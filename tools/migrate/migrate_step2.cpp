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

// migrate_step2.cpp — the engine of pps_migrate_step2 (see migrate_step2.h and pps_migrate_step2.cpp).

#include "migrate_step2.h"
#include "real_path.h"   // realPath: links resolved, junctions too on Windows (#243)

#include "../../src/DomainModel/athlete.h"
#include "../../src/DomainModel/session_identity.h"
#include "../../src/DomainModel/session_input.h"
#include "../../src/Stores/athlete_files.h"
#include "../../src/Stores/athlete_store.h"
#include "../../src/Stores/recording_header.h"
#include "../../src/Stores/rig_store.h"   // RigStore::hasTakenGroup: the write refuses once the Rig keeps the club lengths   // captureEndUtcOf: the inferred end's one rule (J-18)
#include "../../src/Stores/session_diagnostics_files.h"
#include "../../src/Stores/session_files.h"
#include "../../src/Stores/session_store.h"
#include "../../src/Stores/swing_paths.h"
#include "../../src/Stores/swing_store.h"
#include "../../src/Stores/swing_store_repository.h"
#include "../../src/Core/atomic_write.h"
#include "../../src/Core/folder_lock.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <map>
#include <memory>

namespace pinpoint::migrate {

namespace {

// ── Small helpers ────────────────────────────────────────────────────────────────────────────────────

QString clean(const QString &p) { return QDir::cleanPath(QFileInfo(p).absoluteFilePath()); }

QString isoUtc(const QDateTime &t) { return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString(); }

QDateTime parseUtc(const QJsonValue &v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toUTC() : QDateTime();
}

QString compact(const QJsonValue &v)
{
    if (v.isUndefined()) return QStringLiteral("(not held)");
    if (v.isString()) return QLatin1Char('"') + v.toString() + QLatin1Char('"');
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 12);
    return QStringLiteral("null");
}

QString shorten(const QString &s, int n = 160) { return s.size() > n ? s.left(n - 1) + QChar(0x2026) : s; }

QJsonObject readJsonObject(const QString &path, bool *ok, QString *error = nullptr)
{
    *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not a JSON object");
        return {};
    }
    *ok = true;
    return doc.object();
}

QByteArray sha256Of(const QString &path, bool *ok)
{
    QFile f(path);
    *ok = f.open(QIODevice::ReadOnly);
    if (!*ok) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    *ok = h.addData(&f);
    return h.result().toHex();
}

int schemaNumber(const QString &schema, const QString &prefix)
{
    if (!schema.startsWith(prefix)) return 0;
    bool ok = false;
    const int n = schema.mid(prefix.size()).toInt(&ok);
    return ok ? n : 0;
}

// The record exactly as athlete.json holds it: SET-01's keys through AthleteRecord, then JSON's types
// (AthleteStore's asStoredInJson), so a record read from settings and one read from a file compare equal.
QJsonObject storedRecord(const QVariantMap &raw)
{
    const AthleteRecord r = AthleteRecord::fromMap(raw);
    return QJsonObject::fromVariantMap(AthleteRecord::fromMap(QJsonObject::fromVariantMap(r.toMap()).toVariantMap()).toMap());
}

QString recordName(const QJsonObject &record) { return record.value(QStringLiteral("name")).toString(); }

// SessionController::Type as capture.sessionType records it (Swing 0, Wrist 1, GRF 2, Coach 3), by name (DEC-88).
QString typeOfShot(const QJsonValue &v)
{
    if (v.isString()) {
        const QString s = v.toString().toLower();
        return (s == QLatin1String("swing") || s == QLatin1String("wrist") || s == QLatin1String("grf")
                || s == QLatin1String("coach")) ? s : QString();
    }
    if (!v.isDouble()) return {};
    switch (v.toInt(-1)) {
    case 0: return QStringLiteral("swing");
    case 1: return QStringLiteral("wrist");
    case 2: return QStringLiteral("grf");
    case 3: return QStringLiteral("coach");
    default: return {};
    }
}

bool appIsRunning()
{
    QProcess p;
#ifdef Q_OS_WIN
    p.start(QStringLiteral("tasklist"), { QStringLiteral("/fi"), QStringLiteral("imagename eq PinPointStudio.exe") });
    if (!p.waitForFinished(10000)) return true;   // cannot tell: refuse rather than guess
    return QString::fromLocal8Bit(p.readAllStandardOutput()).contains(QLatin1String("PinPointStudio.exe"), Qt::CaseInsensitive);
#else
    p.start(QStringLiteral("pgrep"), { QStringLiteral("-x"), QStringLiteral("PinPointStudio") });
    if (!p.waitForFinished(10000)) return true;
    return p.exitCode() == 0;
#endif
}

// ── What the run reads ──────────────────────────────────────────────────────────────────────────────

struct SwingFacts {
    int       number = 0;
    QString   dir;
    QString   doc;                 // the document's file ("" none)
    QDateTime wallclockUtc;        // the window start (clock.wallclock)
    QDateTime captureEndUtc;       // the window's end (captureEndUtcOf: J-18's one rule, shared with the app)
    QString   type;                // capture.sessionType by name ("" not recorded)
    QString   athleteUuid, athleteName;
};

struct SessionScan {
    QString     folder, name;
    QList<SwingFacts> swings;
    enum class File { Missing, Read, Unreadable, Newer } file = File::Missing;
    QJsonObject json;              // session.json as read
    bool        archived = false;
    QString     mirror;            // archived.json's `location`
    LegacyFolderName parsed;
};

struct AthleteScan {
    QString token, folder;
    bool    exists = false;
    QList<SessionScan> sessions;
    enum class File { Missing, Read, Unreadable, Newer } file = File::Missing;
    QJsonObject json;              // athlete.json as read
};

struct Decisions {
    struct Athlete {
        AthleteId id;
        QMap<QString, QJsonValue> fields;   // key → a value, or { "from": "<machine|library>" }
    };
    QMap<QString, Athlete> athletes;        // by folder token
    QMap<QString, QString> sessionTypes;    // "<token>/<session>" → type
    struct Restore { QString session, from, field; };
    QList<Restore> restores;
    bool loaded = false;
};

// The context of one run.
struct Run {
    Options      opt;
    Result      &res;
    QString      library;                   // cleaned
    QSet<QString> reads;
    QList<AthleteScan> scans;
    Decisions    decisions;
    // This machine's settings, as read from a private copy.
    QList<RecordSource> mine;
    QString      wristSetting, currentAthlete;
    QStringList  priorKeys;
    bool         haveSettings = false;
    QList<RecordSource> other;
    QString      otherWrist;
    // One door per run for M3's reads and writes (the tool's own, never the app's).
    SessionStore                    sessions;
    SwingStoreRecordingRepository   recordings{ sessions };

    explicit Run(Result &r) : res(r) {}
    void read(const QString &path) { if (!path.isEmpty()) reads.insert(clean(path)); }
};

// ── Scanning the library ─────────────────────────────────────────────────────────────────────────────

SwingFacts readSwing(Run &run, const QString &swingDir, int number)
{
    SwingFacts s;
    s.number = number;
    s.dir    = swingDir;
    const SwingStore::DocInfo info = SwingStore::info(swingDir);
    if (!info.exists()) return s;
    s.doc = info.path;
    run.read(info.path);
    QString err;
    const QJsonObject root = SwingStore::loadParts(swingDir, {}, &err);   // the root chunk only (header)
    const QJsonObject clock = root.value(QStringLiteral("clock")).toObject();
    s.wallclockUtc  = parseUtc(clock.value(QStringLiteral("wallclock")));
    s.captureEndUtc = captureEndUtcOf(root);
    s.type     = typeOfShot(root.value(QStringLiteral("capture")).toObject().value(QStringLiteral("sessionType")));
    const QJsonObject a = root.value(QStringLiteral("athlete")).toObject();
    s.athleteUuid = a.value(QStringLiteral("uuid")).toString().toLower().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
    s.athleteName = a.value(QStringLiteral("name")).toString();
    return s;
}

SessionScan scanSession(Run &run, const QString &folder, const QString &athleteToken)
{
    SessionScan s;
    s.folder = folder;
    s.name   = QFileInfo(folder).fileName();
    s.parsed = SessionIdentity::parseLegacyFolderName(s.name, athleteToken);
    const QString file = SessionFiles::sessionFile(folder);
    if (QFileInfo::exists(file)) {
        run.read(file);
        bool ok = false;
        s.json = readJsonObject(file, &ok);
        const int v = schemaNumber(s.json.value(QStringLiteral("schema")).toString(), QStringLiteral("pinpoint.session/"));
        if (!ok || v < 1 || SessionId::fromString(s.json.value(QStringLiteral("sessionId")).toString()).isNull())
            s.file = SessionScan::File::Unreadable;
        else
            s.file = v > 1 ? SessionScan::File::Newer : SessionScan::File::Read;
    }
    const QString marker = SessionFiles::archiveMarker(folder);
    if (QFileInfo::exists(marker)) {
        run.read(marker);
        bool ok = false;
        const QJsonObject m = readJsonObject(marker, &ok);
        s.archived = true;
        s.mirror   = m.value(QStringLiteral("location")).toString();
    }
    for (const QString &name : SwingPaths::swingFolders(folder)) {
        const int n = SwingPaths::swingNumberOf(name);
        if (n > 0) s.swings.append(readSwing(run, folder + QLatin1Char('/') + name, n));
    }
    std::sort(s.swings.begin(), s.swings.end(), [](const SwingFacts &a, const SwingFacts &b) { return a.number < b.number; });
    return s;
}

// A session folder, as SessionStore::readFolder tells one: a session file, a swing, or a name this app makes.
bool isSessionFolder(const QString &folder, const QString &athleteToken)
{
    if (QFileInfo::exists(SessionFiles::sessionFile(folder))) return true;
    if (!SwingPaths::swingFolders(folder).isEmpty()) return true;
    return SessionIdentity::parseLegacyFolderName(QFileInfo(folder).fileName(), athleteToken).isSessionName();
}

AthleteScan scanAthlete(Run &run, const QString &token)
{
    AthleteScan a;
    a.token  = token;
    a.folder = run.library + QLatin1Char('/') + token;
    a.exists = QFileInfo(a.folder).isDir();
    if (!a.exists) return a;
    const QString file = AthleteFiles::athleteFile(a.folder);
    if (QFileInfo::exists(file)) {
        run.read(file);
        bool ok = false;
        a.json = readJsonObject(file, &ok);
        const int v = schemaNumber(a.json.value(QStringLiteral("schema")).toString(), QStringLiteral("pinpoint.athlete/"));
        if (!ok || v < 1 || AthleteId::fromString(a.json.value(QStringLiteral("athleteId")).toString()).isNull())
            a.file = AthleteScan::File::Unreadable;
        else
            a.file = v > 1 ? AthleteScan::File::Newer : AthleteScan::File::Read;
    }
    const QStringList names = QDir(a.folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString folder = a.folder + QLatin1Char('/') + name;
        if (isSessionFolder(folder, token)) a.sessions.append(scanSession(run, folder, token));
    }
    return a;
}

// ── The settings and the other machine's export ─────────────────────────────────────────────────────

QJsonObject decisionsJson(const QString &path, QString *error)
{
    bool ok = false;
    const QJsonObject o = readJsonObject(path, &ok, error);
    if (!ok) return {};
    if (o.value(QStringLiteral("schema")).toString() != QLatin1String(kDecisionsSchema)) {
        if (error) *error = QStringLiteral("not a decisions file (schema \"%1\")").arg(o.value(QStringLiteral("schema")).toString());
        return {};
    }
    return o;
}

bool loadDecisions(Run &run, QString *error)
{
    if (run.opt.decisionsFile.isEmpty()) return true;
    run.read(run.opt.decisionsFile);
    const QJsonObject o = decisionsJson(run.opt.decisionsFile, error);
    if (o.isEmpty()) return false;
    const QJsonObject athletes = o.value(QStringLiteral("athletes")).toObject();
    for (auto it = athletes.constBegin(); it != athletes.constEnd(); ++it) {
        const QJsonObject a = it.value().toObject();
        Decisions::Athlete d;
        d.id = AthleteId::fromString(a.value(QStringLiteral("athleteId")).toString());
        const QJsonObject f = a.value(QStringLiteral("fields")).toObject();
        for (auto ft = f.constBegin(); ft != f.constEnd(); ++ft) d.fields.insert(ft.key(), ft.value());
        run.decisions.athletes.insert(it.key(), d);
    }
    const QJsonObject types = o.value(QStringLiteral("sessionTypes")).toObject();
    for (auto it = types.constBegin(); it != types.constEnd(); ++it) run.decisions.sessionTypes.insert(it.key(), it.value().toString());
    for (const QJsonValue &v : o.value(QStringLiteral("restore")).toArray()) {
        const QJsonObject r = v.toObject();
        run.decisions.restores.append({ r.value(QStringLiteral("session")).toString(), r.value(QStringLiteral("from")).toString(),
                                        r.value(QStringLiteral("field")).toString() });
    }
    run.decisions.loaded = true;
    return true;
}

bool loadOther(Run &run, QString *error)
{
    if (run.opt.otherMachineExport.isEmpty()) return true;
    run.read(run.opt.otherMachineExport);
    bool ok = false;
    const QJsonObject o = readJsonObject(run.opt.otherMachineExport, &ok, error);
    if (!ok) return false;
    if (o.value(QStringLiteral("schema")).toString() != QLatin1String(kExportSchema)) {
        *error = QStringLiteral("not a records export (schema \"%1\")").arg(o.value(QStringLiteral("schema")).toString());
        return false;
    }
    run.res.otherMachine = o.value(QStringLiteral("machine")).toString();
    if (run.res.otherMachine.isEmpty()) run.res.otherMachine = QStringLiteral("the other machine");
    run.otherWrist = o.value(QStringLiteral("wristReferenceSwingDir")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("athletes")).toArray()) {
        const QJsonObject a = v.toObject();
        RecordSource r;
        r.origin      = run.res.otherMachine;
        r.id          = AthleteId::fromString(a.value(QStringLiteral("athleteId")).toString());
        r.settingsKey = a.value(QStringLiteral("settingsKey")).toString();
        r.record      = storedRecord(a.value(QStringLiteral("record")).toObject().toVariantMap());
        if (!r.id.isNull()) run.other.append(r);
    }
    return true;
}

// ── M1 ───────────────────────────────────────────────────────────────────────────────────────────────

QString thisOrigin(const Run &run) { return QStringLiteral("this machine (%1)").arg(run.res.machine); }

void planAthletes(Run &run)
{
    // Every folder that is an athlete's, or that a record names.
    QMap<QString, AthleteScan *> byToken;
    for (AthleteScan &a : run.scans) byToken.insert(a.token, &a);
    QHash<AthleteId, QString> tokenOfId;   // athlete.json's ids → their folder
    for (const AthleteScan &a : run.scans)
        if (a.file == AthleteScan::File::Read || a.file == AthleteScan::File::Newer)
            tokenOfId.insert(AthleteId::fromString(a.json.value(QStringLiteral("athleteId")).toString()), a.token);

    const auto tokenFor = [&](const RecordSource &r) {
        if (const auto it = tokenOfId.constFind(r.id); it != tokenOfId.constEnd()) return *it;   // athlete.json wins
        const QString name = recordName(r.record);
        return SwingPaths::sanitise(!name.trimmed().isEmpty() ? name : r.id.toString());
    };

    QMap<QString, AthletePlan> plans;
    const auto planFor = [&](const QString &token) -> AthletePlan & {
        auto it = plans.find(token);
        if (it == plans.end()) {
            AthletePlan p;
            p.token  = token;
            p.folder = run.library + QLatin1Char('/') + token;
            p.folderExists = QFileInfo(p.folder).isDir();
            it = plans.insert(token, p);
        }
        return *it;
    };
    for (const AthleteScan &a : run.scans) {
        if (a.sessions.isEmpty() && a.file == AthleteScan::File::Missing) continue;   // reached by a record only
        planFor(a.token);
    }
    for (const RecordSource &r : run.mine) planFor(tokenFor(r)).thisMachine.append(r);
    for (const RecordSource &r : run.other) planFor(tokenFor(r)).otherMachine.append(r);

    for (auto it = plans.begin(); it != plans.end(); ++it) {
        AthletePlan &p = *it;
        const AthleteScan *scan = byToken.value(p.token);
        if (scan) {
            p.sessions = int(scan->sessions.size());
            for (const SessionScan &s : scan->sessions)
                for (const SwingFacts &w : s.swings) {
                    if (!w.athleteUuid.isEmpty()) p.headerIds[w.athleteUuid] += 1;
                    if (!w.athleteName.isEmpty()) p.headerNames[w.athleteName] += 1;
                }
            if (scan->file == AthleteScan::File::Read || scan->file == AthleteScan::File::Newer) {
                RecordSource l;
                l.origin = QStringLiteral("library");
                l.id     = AthleteId::fromString(scan->json.value(QStringLiteral("athleteId")).toString());
                l.record = storedRecord(scan->json.value(QStringLiteral("record")).toObject().toVariantMap());
                p.library = l;
                p.libraryReadOnly = scan->file == AthleteScan::File::Newer;
            } else if (scan->file == AthleteScan::File::Unreadable) {
                p.libraryReadOnly = true;
            }
        }
        // The folder's swings: two names (two athletes in one folder?) and uuids no record has (J-39).
        if (p.headerNames.size() > 1)
            p.notes << QStringLiteral("its swings name %1 athletes (%2): a folder holding more than one athlete is reported, never split")
                           .arg(p.headerNames.size()).arg(QStringList(p.headerNames.keys()).join(QStringLiteral(", ")));
        if (p.headerIds.size() > 1)
            p.notes << QStringLiteral("its swings record %1 different athlete uuids").arg(p.headerIds.size());

        // Outcome.
        if (scan && scan->file == AthleteScan::File::Unreadable) {
            p.outcome = AthletePlan::Outcome::Blocked;
            p.why     = QStringLiteral("its athlete.json cannot be read: it is left for someone to look at, never overwritten");
            continue;
        }
        if (p.thisMachine.size() > 1 || p.otherMachine.size() > 1) {
            p.outcome = AthletePlan::Outcome::Blocked;
            p.why     = QStringLiteral("more than one record names this folder (look-alike names, J-30): rename one in the app first; "
                                       "the tool never splits a folder");
            continue;
        }
        const RecordSource *T = p.thisMachine.isEmpty() ? nullptr : &p.thisMachine.first();
        const RecordSource *O = p.otherMachine.isEmpty() ? nullptr : &p.otherMachine.first();
        const RecordSource *L = p.library ? &*p.library : nullptr;
        if (!L && !T) {
            if (O) {
                p.outcome = AthletePlan::Outcome::NothingHere;
                p.why     = QStringLiteral("only %1 holds this athlete's record: its own run migrates it").arg(O->origin);
            } else {
                p.outcome = AthletePlan::Outcome::NoRecord;
                p.why     = QStringLiteral("no athlete record names this folder (an athlete renamed since, E-02, or one only another machine knows)");
                // Whose sessions these are, by the uuid the swings recorded: a record's own id is the clue.
                for (auto h = p.headerIds.constBegin(); h != p.headerIds.constEnd(); ++h)
                    for (const QList<RecordSource> *list : { &run.mine, &run.other })
                        for (const RecordSource &r : *list)
                            if (r.id.toString() == h.key())
                                p.notes << QStringLiteral("its swings record %1, the id of %2's record \"%3\", whose name now gives the "
                                                          "folder %4 (renamed since? E-02). Moving the sessions is by hand; the tool never "
                                                          "moves or splits a folder")
                                               .arg(h.key(), r.origin, recordName(r.record), tokenFor(r));
            }
            continue;
        }
        if (p.libraryReadOnly) {
            p.outcome = AthletePlan::Outcome::Blocked;
            p.why     = QStringLiteral("its athlete.json was written by a newer PinPoint Studio: not changed by this tool");
            p.id      = L ? L->id : AthleteId();
            continue;
        }
        if (!T) {
            // The library holds the record and this machine none: nothing to do here (the other machine's
            // copy, if it has one, is its own run's business).
            p.outcome = AthletePlan::Outcome::AlreadyInLibrary;
            p.id      = L->id;
            p.record  = L->record;
            if (O) p.notes << QStringLiteral("%1 holds a copy of this record too: its own run reconciles it").arg(O->origin);
            const AthleteId want = run.decisions.athletes.value(p.token).id;
            if (!want.isNull() && want != L->id)
                p.notes << QStringLiteral("**the decisions name id %1, but the library's athlete.json holds %2**").arg(want.toString(), L->id.toString());
            continue;
        }
        if (L) O = nullptr;   // the library's record stands; only this machine's copy is compared with it
        const Decisions::Athlete dec = run.decisions.athletes.value(p.token);
        QStringList blocked;

        // THE ID (J-12: M2 freezes every old session's id from it, so it is never settled silently).
        //   · the library's athlete.json holds one: that one; a decision naming another is refused;
        //   · a decision names one: honoured when it is this machine's, the other machine's, or a uuid the
        //     folder's swings record (J-58's recommendation); any other uuid is refused, with the reason;
        //   · no decision: this machine's id, unless the other machine gives another (pick one), or the
        //     folder's swings record none of it (J-58: needs a decision before M1 runs).
        const QString swingIds = QStringList(p.headerIds.keys()).join(QStringLiteral(", "));
        if (L) {
            p.id = L->id;
            if (!dec.id.isNull() && dec.id != L->id)
                blocked << QStringLiteral("the decisions name id %1, but the library's athlete.json already holds %2")
                               .arg(dec.id.toString(), L->id.toString());
        } else if (!dec.id.isNull()) {
            const bool known = dec.id == T->id || (O && dec.id == O->id) || p.headerIds.contains(dec.id.toString());
            p.id = known ? dec.id : T->id;
            if (!known) {
                p.idProvisional = true;
                blocked << QStringLiteral("the decisions name id %1, which is neither machine's record id nor a uuid this "
                                          "folder's swings record (%2): refused").arg(dec.id.toString(), swingIds.isEmpty() ? QStringLiteral("none") : swingIds);
            } else {
                p.notes << QStringLiteral("the id is the decided one, %1").arg(dec.id.toString());
            }
        } else if (O && O->id != T->id) {
            p.id = T->id;
            p.idProvisional = true;
            blocked << QStringLiteral("the two machines give this athlete different ids (%1 here, %2 on %3): pick one "
                                      "(decisions: athletes.%4.athleteId)").arg(T->id.toString(), O->id.toString(), O->origin, p.token);
        } else {
            p.id = T->id;
            if (!p.headerIds.isEmpty() && !p.headerIds.contains(p.id.toString())) {
                p.idProvisional = true;
                blocked << QStringLiteral("no swing in this folder records the id M1 would settle (%1); they record %2. Every old "
                                          "session's id follows this choice (J-12, J-58): pick one (decisions: athletes.%3.athleteId)")
                               .arg(p.id.toString(), swingIds, p.token);
            }
        }
        if (!p.headerIds.isEmpty() && !p.headerIds.contains(p.id.toString()) && !p.idProvisional)
            p.notes << QStringLiteral("no swing records this id; they record %1 (J-39: a legacy session's id comes from the owner, this id)")
                           .arg(swingIds);

        // The fields: every key where the sources differ.
        QJsonObject base = L ? L->record : T->record;
        QStringList keys;
        for (const RecordSource *s : { L, T, O })
            if (s) for (const QString &k : s->record.keys()) if (!keys.contains(k)) keys << k;
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys) {
            FieldDifference d;
            d.key          = k;
            d.thisMachine  = T ? T->record.value(k) : QJsonValue(QJsonValue::Undefined);
            d.otherMachine = O ? O->record.value(k) : QJsonValue(QJsonValue::Undefined);
            d.library      = L ? L->record.value(k) : QJsonValue(QJsonValue::Undefined);
            // A conflict only among the sources that HOLD the key: a key one record lacks (a bag never stored, the
            // ★ M4 put on the library's record) loses nothing when the other's value is taken — the union.
            QList<QJsonValue> held;
            for (const RecordSource *s : { L, T, O }) if (s && s->record.contains(k)) held << s->record.value(k);
            bool same = true;
            for (const QJsonValue &v : held) same = same && v == held.first();
            if (same) {
                if (!held.isEmpty() && !base.contains(k)) base.insert(k, held.first());
                continue;
            }
            if (dec.fields.contains(k)) {
                const QJsonValue pick = dec.fields.value(k);
                const QString from = pick.isObject() ? pick.toObject().value(QStringLiteral("from")).toString() : QString();
                if (!from.isEmpty()) {
                    if (from == run.res.machine && T)           { d.chosen = d.thisMachine; d.decided = true; }
                    // Where the library already holds the record, the other machine's run wrote it with this very
                    // decision: "the other machine's value" is the library's now.
                    else if (L && (from == QLatin1String("library") || from != run.res.machine)) { d.chosen = d.library; d.decided = true; }
                    else if (O && from == O->origin)            { d.chosen = d.otherMachine; d.decided = true; }
                } else {
                    d.chosen = pick; d.decided = true;
                }
            }
            if (!d.decided)
                blocked << QStringLiteral("%1 differs").arg(k);
            else if (d.chosen.isUndefined() || d.chosen.isNull())
                base.remove(k);
            else
                base.insert(k, d.chosen);
            p.differences.append(d);
        }
        p.record = storedRecord(base.toVariantMap());

        if (!blocked.isEmpty()) {
            p.outcome = AthletePlan::Outcome::Blocked;
            p.why     = blocked.join(QStringLiteral("; "));
            continue;
        }
        if (!L)
            p.outcome = AthletePlan::Outcome::WriteNew;
        else
            p.outcome = p.record == L->record ? AthletePlan::Outcome::AlreadyInLibrary : AthletePlan::Outcome::Update;
        if (T) p.settingsGroupsToRemove << T->settingsKey;
        if (!p.folderExists) p.notes << QStringLiteral("no folder yet (an athlete with no session): the folder is made");
    }
    for (const AthletePlan &p : plans) run.res.athletes.append(p);

    // This machine's settings that name an id M1 replaces (the same athlete under the settled id).
    for (const AthletePlan &p : std::as_const(run.res.athletes)) {
        if (p.thisMachine.isEmpty() || p.id.isNull() || p.thisMachine.first().id == p.id) continue;
        const QString old = p.thisMachine.first().id.toString();
        if (run.currentAthlete.compare(old, Qt::CaseInsensitive) == 0)
            run.res.otherIdReferences << QStringLiteral("currentAthleteUuid names %1 (%2): becomes %3").arg(old, p.token, p.id.toString());
        for (const QString &k : run.priorKeys)
            if (k.startsWith(old + QLatin1Char('|'), Qt::CaseInsensitive))
                run.res.otherIdReferences << QStringLiteral("analysis/clubLenPrior \"%1\": re-keyed to %2").arg(k, p.id.toString());
    }
}

const AthletePlan *athletePlan(const Result &r, const QString &token)
{
    for (const AthletePlan &p : r.athletes) if (p.token == token) return &p;
    return nullptr;
}

bool settles(const AthletePlan *p)
{
    return p && (p->outcome == AthletePlan::Outcome::WriteNew || p->outcome == AthletePlan::Outcome::Update
                 || p->outcome == AthletePlan::Outcome::AlreadyInLibrary);
}

// ── M2 ───────────────────────────────────────────────────────────────────────────────────────────────

void planSessions(Run &run)
{
    const QTimeZone zone = QTimeZone::systemTimeZone();
    for (const AthleteScan &a : run.scans) {
        const AthletePlan *ap = athletePlan(run.res, a.token);
        QList<SessionPlan> mine;
        for (const SessionScan &s : a.sessions) {
            SessionPlan p;
            p.athleteToken = a.token;
            p.folder       = s.folder;
            p.name         = s.name;
            p.swings       = int(s.swings.size());
            p.archived     = s.archived;
            p.mirrorFolder = s.mirror;
            p.nameDay      = s.parsed.day;
            p.nameType     = SessionIdentity::typeFromToken(s.parsed.typeToken);

            // The capture times and the shots' type.
            for (const SwingFacts &w : s.swings) {
                p.shotTypes[w.type] += 1;
                if (!w.athleteUuid.isEmpty() && !p.swingUuids.contains(w.athleteUuid)) p.swingUuids << w.athleteUuid;
                if (!w.wallclockUtc.isValid()) continue;
                const QDateTime end = w.captureEndUtc;
                if (!p.startUtc.isValid() || w.wallclockUtc < p.startUtc) p.startUtc = w.wallclockUtc;
                if (!p.endUtc.isValid() || end > p.endUtc) p.endUtc = end;
            }
            if (!p.startUtc.isValid() && s.parsed.day.isValid()) {
                p.startUtc = QDateTime(s.parsed.day, QTime(0, 0), zone).toUTC();
                p.endUtc   = {};
                p.timesFromFolderDate = true;
            }
            QStringList shotTypes;
            for (auto it = p.shotTypes.constBegin(); it != p.shotTypes.constEnd(); ++it) if (!it.key().isEmpty()) shotTypes << it.key();
            if (shotTypes.size() > 1) {
                p.typeDisagreement = p.typeNeedsDecision = true;
                const QString pick = run.decisions.sessionTypes.value(a.token + QLatin1Char('/') + s.name);
                p.type = pick;
            } else if (shotTypes.size() == 1) {
                p.type = shotTypes.first();
                p.typeDisagreement = !p.nameType.isEmpty() && p.nameType != p.type;
            } else {
                p.type = p.nameType;
            }

            // The owner (J-39): the athlete M1 settles; else, with no record, the swings' one uuid.
            if (settles(ap)) {
                p.owner = ap->id;
            } else if (ap && ap->outcome == AthletePlan::Outcome::Blocked && !ap->id.isNull()) {
                p.owner = ap->id;
                p.ownerProvisional = true;
            } else {
                QSet<QString> ids;
                for (const SwingFacts &w : s.swings) if (!w.athleteUuid.isEmpty()) ids.insert(w.athleteUuid);
                if (ids.size() == 1) {
                    p.owner = AthleteId::fromString(*ids.begin());
                    p.ownerFromHeaders = true;
                }
            }

            switch (s.file) {
            case SessionScan::File::Unreadable:
                p.outcome = SessionPlan::Outcome::Skipped;
                p.why     = QStringLiteral("its session.json cannot be read: left as it is, never overwritten");
                break;
            case SessionScan::File::Newer:
                p.outcome = SessionPlan::Outcome::Skipped;
                p.why     = QStringLiteral("its session.json was written by a newer PinPoint Studio");
                p.id      = SessionId::fromString(s.json.value(QStringLiteral("sessionId")).toString());
                break;
            case SessionScan::File::Read: {
                p.id = SessionId::fromString(s.json.value(QStringLiteral("sessionId")).toString());
                p.inProgress = s.json.value(QStringLiteral("status")).toString() == QLatin1String("inProgress");
                const QJsonObject origin = s.json.value(QStringLiteral("origin")).toObject();
                const QDateTime   filed  = parseUtc(s.json.value(QStringLiteral("startedAt")));
                const AthleteId   fileAthlete = AthleteId::fromString(s.json.value(QStringLiteral("athleteId")).toString());
                if (!fileAthlete.isNull() && !p.owner.isNull() && fileAthlete != p.owner && !p.ownerProvisional)
                    p.why = QStringLiteral("its session.json names athlete %1, not the folder's owner %2: reported, never re-owned")
                                .arg(fileAthlete.toString(), p.owner.toString());
                p.type = s.json.value(QStringLiteral("type")).toString();
                p.typeDisagreement = p.typeNeedsDecision = false;
                const bool appBackfilled = origin.value(QStringLiteral("kind")).toString() == QLatin1String("backfilled")
                                        && origin.value(QStringLiteral("tool")).toString() != QLatin1String(kToolName);
                const bool fromCapture = p.startUtc.isValid() && !p.timesFromFolderDate;
                if (p.inProgress) {
                    p.outcome = SessionPlan::Outcome::Skipped;
                    p.why     = QStringLiteral("in progress (its session.json says so): never touched (D11)");
                } else if (appBackfilled && fromCapture && p.startUtc != filed) {
                    // J-19: the app wrote it at a resume, its start inferred from folder times; the swings know better.
                    p.outcome = SessionPlan::Outcome::CorrectStart;
                    p.previousStartUtc = filed;
                } else {
                    p.outcome = origin.value(QStringLiteral("tool")).toString() == QLatin1String(kToolName)
                                    ? SessionPlan::Outcome::AlreadyMigrated : SessionPlan::Outcome::HasFile;
                    if (appBackfilled && !fromCapture) p.why = QStringLiteral("backfilled by the app; no capture time to correct its start from");
                }
                if (p.outcome != SessionPlan::Outcome::CorrectStart) p.startUtc = filed;
                p.endUtc = parseUtc(s.json.value(QStringLiteral("endedAt")));
                p.timesFromFolderDate = false;
                if (p.inProgress) run.res.inProgress << s.folder;
                break;
            }
            case SessionScan::File::Missing: {
                p.id = SessionIdentity::legacySessionId(p.owner, s.name);
                if (p.owner.isNull()) {
                    p.outcome = SessionPlan::Outcome::Skipped;
                    p.why     = s.swings.isEmpty()
                        ? QStringLiteral("no owner: no athlete record names its folder, and it holds no swing folder to name one")
                        : QStringLiteral("no owner: no athlete record names its folder, and its swings do not agree on one athlete");
                } else if (p.ownerFromHeaders) {
                    p.outcome = SessionPlan::Outcome::Skipped;
                    p.why     = QStringLiteral("no athlete record names its folder: its id comes from its swings' athlete.uuid in memory, "
                                               "as today; not written until the folder has an owner");
                } else if (p.ownerProvisional) {
                    p.outcome = SessionPlan::Outcome::Skipped;
                    p.why     = QStringLiteral("waits for M1: its athlete's id is not settled (%1)").arg(ap ? ap->why : QString());
                } else if (!p.startUtc.isValid()) {
                    p.outcome = SessionPlan::Outcome::Skipped;
                    p.why     = QStringLiteral("no swing with a capture time and no date in its name: no start to give it");
                } else if (p.typeNeedsDecision && p.type.isEmpty()) {
                    p.outcome = SessionPlan::Outcome::Skipped;
                    p.why     = QStringLiteral("its shots disagree on the type: pick one (decisions: sessionTypes.\"%1/%2\")").arg(a.token, s.name);
                } else {
                    p.outcome = SessionPlan::Outcome::Write;
                }
                break;
            }
            }
            mine.append(p);
        }

        // Same-day order: today's (the names' counters) against the start times (§5.2: parity B's list).
        QMap<QDate, QList<const SessionPlan *>> byDay;
        for (const SessionPlan &p : mine)
            if (p.startUtc.isValid()) byDay[p.startUtc.toTimeZone(zone).date()].append(&p);
        for (auto it = byDay.cbegin(); it != byDay.cend(); ++it) {
            if (it->size() < 2) continue;
            QList<const SessionPlan *> byName = *it, byStart = *it;
            const auto counter = [&](const SessionPlan *p) {
                return SessionIdentity::parseLegacyFolderName(p->name, a.token).counter;
            };
            std::sort(byName.begin(), byName.end(), [&](const SessionPlan *x, const SessionPlan *y) {
                const QDate dx = x->nameDay, dy = y->nameDay;
                if (dx != dy) return dx < dy;
                if (counter(x) != counter(y)) return counter(x) < counter(y);
                return x->name < y->name;
            });
            std::sort(byStart.begin(), byStart.end(), [](const SessionPlan *x, const SessionPlan *y) {
                return x->startUtc != y->startUtc ? x->startUtc < y->startUtc : x->name < y->name;
            });
            if (byName == byStart) continue;
            DayOrderChange c;
            c.athleteToken = a.token;
            c.day = it.key();
            for (const SessionPlan *p : byName) c.byName << p->name;
            for (const SessionPlan *p : byStart) c.byStart << p->name;
            run.res.orderChanges.append(c);
        }
        run.res.sessions.append(mine);
    }
}

const SessionPlan *sessionPlan(const Result &r, const QString &token, const QString &name)
{
    for (const SessionPlan &p : r.sessions) if (p.athleteToken == token && p.name == name) return &p;
    return nullptr;
}

// ── M3 ───────────────────────────────────────────────────────────────────────────────────────────────

QString describeFocus(const DeclaredFocus &f)
{
    return f.conditionId.isEmpty() ? QString() : QStringLiteral("%1 from shot %2").arg(f.conditionId).arg(f.fromShot);
}
QString describeRun(const IntentRun &r)
{
    return r.shape.isEmpty() ? QString() : QStringLiteral("%1 from shot %2").arg(r.shape).arg(r.fromShot);
}
QString describeScreens(const QHash<QString, bool> &s)
{
    QStringList out;
    for (auto it = s.constBegin(); it != s.constEnd(); ++it) out << it.key() + (it.value() ? QStringLiteral("=yes") : QStringLiteral("=no"));
    std::sort(out.begin(), out.end());
    return out.join(QStringLiteral(", "));
}
QString describeCorrections(const std::vector<analysis::ShotCorrection> &list)
{
    QStringList out;
    for (const auto &c : list) out << QStringLiteral("%1: %2→%3").arg(c.family, c.from, c.to);
    return out.join(QStringLiteral("; "));
}

// The golfer's whole resolved input, as one comparable string (the check before keys leave diagnostics.json).
QString describe(const GolferInput &g)
{
    QStringList parts;
    parts << QStringLiteral("focus=") + g.focus.conditionId + QLatin1Char('@') + QString::number(g.focus.fromShot)
          << QStringLiteral("miss=") + g.declaredMiss
          << QStringLiteral("screens=") + describeScreens(g.screens)
          << QStringLiteral("run=") + g.intentRun.shape + QLatin1Char('@') + QString::number(g.intentRun.fromShot);
    QList<int> shots = g.shotIntent.keys();
    std::sort(shots.begin(), shots.end());
    for (int n : shots) parts << QStringLiteral("intent%1=%2").arg(n).arg(g.shotIntent.value(n));
    QList<int> cs = g.corrections.keys();
    std::sort(cs.begin(), cs.end());
    for (int n : cs) parts << QStringLiteral("corr%1=%2").arg(n).arg(describeCorrections(g.corrections.value(n)));
    return parts.join(QLatin1Char('|'));
}

// The older shape's values, as the model reads them, from one envelope's keys.
struct LegacyValues {
    DeclaredFocus        focus;
    QString              miss;
    QHash<QString, bool> screens;
    IntentRun            run;
    QHash<int, QString>  shotIntent;
    QHash<int, std::vector<analysis::ShotCorrection>> corrections;
};
LegacyValues legacyValues(const QJsonObject &legacy)
{
    LegacyValues v;
    v.focus       = session_diagnostics_detail::legacyFocus(legacy);
    v.miss        = session_diagnostics_detail::legacyMiss(legacy);
    v.screens     = session_diagnostics_detail::legacyScreens(legacy);
    v.run         = session_diagnostics_detail::legacyRun(legacy);
    v.shotIntent  = analysis::shotIntentsFromJson(legacy.value(QStringLiteral("shotIntent")).toObject());
    v.corrections = analysis::shotCorrectionsFromJson(legacy.value(QStringLiteral("corrections")).toObject());
    return v;
}

QJsonArray correctionsJson(const std::vector<analysis::ShotCorrection> &list)
{
    QJsonArray a;
    for (const auto &c : list)
        a.append(QJsonObject{ { QStringLiteral("family"), c.family }, { QStringLiteral("from"), c.from }, { QStringLiteral("to"), c.to } });
    return a;
}

QJsonObject focusJson(const DeclaredFocus &f)
{
    return QJsonObject{ { QStringLiteral("conditionId"), f.conditionId }, { QStringLiteral("fromShot"), f.fromShot } };
}
QJsonObject screensJson(const QHash<QString, bool> &s)
{
    QJsonObject o;
    for (auto it = s.constBegin(); it != s.constEnd(); ++it) o.insert(it.key(), it.value());
    return o;
}
QJsonArray runsJson(const IntentRun &r)
{
    QJsonArray a;
    if (!r.shape.isEmpty())
        a.append(QJsonObject{ { QStringLiteral("fromShot"), r.fromShot }, { QStringLiteral("shape"), r.shape } });
    return a;
}

SessionInput sessionInputFrom(const QJsonObject &o)
{
    SessionInput in;
    if (o.contains(QStringLiteral("declaredFocus"))) {
        const QJsonObject d = o.value(QStringLiteral("declaredFocus")).toObject();
        in.declaredFocus = DeclaredFocus{ d.value(QStringLiteral("conditionId")).toString(), d.value(QStringLiteral("fromShot")).toInt(-1) };
    }
    if (o.contains(QStringLiteral("declaredMiss"))) in.declaredMiss = o.value(QStringLiteral("declaredMiss")).toString();
    if (o.contains(QStringLiteral("screenAnswers"))) {
        QMap<QString, bool> m;
        const QJsonObject a = o.value(QStringLiteral("screenAnswers")).toObject();
        for (auto it = a.constBegin(); it != a.constEnd(); ++it) m.insert(it.key(), it.value().toBool());
        in.screenAnswers = m;
    }
    if (o.contains(QStringLiteral("intentRuns"))) {
        QVector<IntentRun> runs;
        for (const QJsonValue &v : o.value(QStringLiteral("intentRuns")).toArray()) {
            const QJsonObject r = v.toObject();
            runs.append(IntentRun{ r.value(QStringLiteral("fromShot")).toInt(-1), r.value(QStringLiteral("shape")).toString() });
        }
        in.intentRuns = runs;
    }
    return in;
}

bool hasSwing(const SessionScan &s, int n)
{
    for (const SwingFacts &w : s.swings) if (w.number == n && !w.doc.isEmpty()) return true;
    return false;
}

void planGolferInput(Run &run)
{
    for (const AthleteScan &a : run.scans) {
        bool athleteTouched = false;
        for (const SessionScan &s : a.sessions) {
            GolferPlan g;
            g.athleteToken = a.token;
            g.folder       = s.folder;
            g.name         = s.name;

            // The backups (FILE-SESS-12, and a set-aside unreadable copy): every diagnostics.json.<tag>.
            const QStringList backups = QDir(s.folder).entryList({ SessionFiles::diagnosticsFileName() + QStringLiteral(".*") },
                                                                 QDir::Files, QDir::Name);
            const QString live = SessionFiles::diagnosticsFile(s.folder);
            const bool hasLive = QFileInfo::exists(live);
            if (!hasLive && backups.isEmpty()) continue;   // nothing of the older shape anywhere

            const SessionPlan *sp = sessionPlan(run.res, a.token, s.name);
            if (sp && sp->inProgress) { g.skipped = true; g.why = QStringLiteral("in progress: never touched (D11)"); }
            else if (s.archived) { g.skipped = true; g.why = QStringLiteral("archived: its swings are stubs; restore it, then migrate"); }

            DiagnosticsRead d;
            if (hasLive) {
                run.read(live);
                d = SessionDiagnosticsFiles::readDiagnostics(s.folder);
                if (d.state == DiagnosticsRead::State::Newer) { g.skipped = true; g.why = QStringLiteral("its diagnostics.json is a newer build's"); }
                else if (!d.parsed()) { g.skipped = true; g.why = QStringLiteral("its diagnostics.json cannot be read (%1)").arg(d.error); }
            }
            const QString inputFile = SessionFiles::sessionInputFile(s.folder);
            if (QFileInfo::exists(inputFile)) run.read(inputFile);
            const SessionInputRead sin = SessionDiagnosticsFiles::readSessionInput(s.folder);
            if (sin.readOnly() && !g.skipped) { g.skipped = true; g.why = QStringLiteral("its session_input.json is a newer build's, or cannot be read"); }

            const QJsonObject legacy = d.parsed() ? d.envelope.legacyGolferInput : QJsonObject();
            for (const SwingFacts &w : s.swings) run.read(w.doc);   // golferInput reads every review
            const GolferInput before = SessionDiagnosticsFiles::golferInput(s.folder, legacy, run.recordings, &sin);
            g.legacyKeys = legacy.keys();
            const LegacyValues lv = legacyValues(legacy);

            // Session level.
            QJsonObject fields;   // what session_input.json gains
            bool sessionMoves = false;
            const auto sessionField = [&](const QString &key, const QString &target, bool held, const QString &value,
                                          bool fileHas, const QString &fileValue) {
                GolferMove m;
                m.key = key; m.target = QStringLiteral("session_input.json ") + target; m.value = value;
                if (!held) m.action = GolferMove::Action::Empty;
                else if (fileHas) m.action = fileValue == value ? GolferMove::Action::AlreadyThere : GolferMove::Action::NewHomeDiffers;
                else { m.action = GolferMove::Action::Move; sessionMoves = true; }
                g.moves.append(m);
            };
            if (legacy.contains(QStringLiteral("intent"))) {
                sessionField(QStringLiteral("intent.focusConditionId/focusFromShot"), QStringLiteral("declaredFocus"),
                             !lv.focus.conditionId.isEmpty(), describeFocus(lv.focus), bool(sin.input.declaredFocus),
                             sin.input.declaredFocus ? describeFocus(*sin.input.declaredFocus) : QString());
                sessionField(QStringLiteral("intent.declaredMiss"), QStringLiteral("declaredMiss"), !lv.miss.isEmpty(), lv.miss,
                             bool(sin.input.declaredMiss), sin.input.declaredMiss.value_or(QString()));
            }
            if (legacy.contains(QStringLiteral("screens"))) {
                QHash<QString, bool> fileScreens;
                if (sin.input.screenAnswers)
                    for (auto it = sin.input.screenAnswers->constBegin(); it != sin.input.screenAnswers->constEnd(); ++it)
                        fileScreens.insert(it.key(), it.value());
                sessionField(QStringLiteral("screens"), QStringLiteral("screenAnswers"), !lv.screens.isEmpty(), describeScreens(lv.screens),
                             bool(sin.input.screenAnswers), describeScreens(fileScreens));
            }
            if (legacy.contains(QStringLiteral("intentFrom"))) {
                const IntentRun fileRun = sin.input.intentRuns && !sin.input.intentRuns->isEmpty() ? sin.input.intentRuns->last() : IntentRun{};
                sessionField(QStringLiteral("intentFrom"), QStringLiteral("intentRuns"), !lv.run.shape.isEmpty(), describeRun(lv.run),
                             bool(sin.input.intentRuns), describeRun(fileRun));
            }
            if (sessionMoves) {
                // The file becomes the whole of the session's input: each field it lacks, as the golfer has it now.
                if (!sin.input.declaredFocus) fields.insert(QStringLiteral("declaredFocus"), focusJson(before.focus));
                if (!sin.input.declaredMiss)  fields.insert(QStringLiteral("declaredMiss"), before.declaredMiss);
                if (!sin.input.screenAnswers) fields.insert(QStringLiteral("screenAnswers"), screensJson(before.screens));
                if (!sin.input.intentRuns)    fields.insert(QStringLiteral("intentRuns"), runsJson(before.intentRun));
            }

            // Shot level.
            QSet<QString> goneKeys;
            const QJsonObject rawIntent = legacy.value(QStringLiteral("shotIntent")).toObject();
            const QJsonObject rawCorr   = legacy.value(QStringLiteral("corrections")).toObject();
            QMap<int, ShotReview> reviews;
            const auto reviewOf = [&](int n) -> ShotReview {
                if (!reviews.contains(n)) {
                    const ShotRef shot = run.recordings.shotOfFolder(s.folder + QLatin1Char('/') + SwingPaths::swingFolderName(n));
                    reviews.insert(n, run.recordings.readReview(shot));
                }
                return reviews.value(n);
            };
            for (auto it = rawIntent.constBegin(); it != rawIntent.constEnd(); ++it) {
                const int n = it.key().toInt();
                GolferMove m;
                m.key = QStringLiteral("shotIntent.") + it.key();
                m.target = QStringLiteral("%1 review.shotIntent").arg(SwingPaths::swingFolderName(n));
                m.value = it.value().toString();
                if (!lv.shotIntent.contains(n)) m.action = GolferMove::Action::Empty;
                else if (!hasSwing(s, n)) { m.action = GolferMove::Action::ShotGone; goneKeys.insert(QStringLiteral("shotIntent")); }
                else {
                    const ShotReview r = reviewOf(n);
                    if (r.hasShotIntent) m.action = r.shotIntent == m.value ? GolferMove::Action::AlreadyThere : GolferMove::Action::NewHomeDiffers;
                    else g.reviewEdits[n].insert(QStringLiteral("shotIntent"), m.value);
                }
                g.moves.append(m);
            }
            for (auto it = rawCorr.constBegin(); it != rawCorr.constEnd(); ++it) {
                const int n = it.key().toInt();
                GolferMove m;
                m.key = QStringLiteral("corrections.") + it.key();
                m.target = QStringLiteral("%1 review.labelCorrections").arg(SwingPaths::swingFolderName(n));
                m.value = lv.corrections.contains(n) ? describeCorrections(lv.corrections.value(n)) : QString();
                if (!lv.corrections.contains(n)) m.action = GolferMove::Action::Empty;
                else if (!hasSwing(s, n)) { m.action = GolferMove::Action::ShotGone; goneKeys.insert(QStringLiteral("corrections")); }
                else {
                    const ShotReview r = reviewOf(n);
                    if (r.hasLabelCorrections) {
                        std::vector<analysis::ShotCorrection> have;
                        for (const LabelCorrection &c : r.labelCorrections) have.push_back({ c.family, c.from, c.to });
                        m.action = describeCorrections(have) == m.value ? GolferMove::Action::AlreadyThere : GolferMove::Action::NewHomeDiffers;
                    } else {
                        g.reviewEdits[n].insert(QStringLiteral("labelCorrections"), correctionsJson(lv.corrections.value(n)));
                    }
                }
                g.moves.append(m);
            }
            for (const QString &k : g.legacyKeys) (goneKeys.contains(k) ? g.keysKept : g.keysToRemove) << k;

            // Golfer input only a backup holds (H-04: the 10 Jul focus is the known case).
            const QString sessionKey = a.token + QLatin1Char('/') + s.name;
            for (const QString &b : backups) {
                if (b.contains(QLatin1String(".partial")) || b.endsWith(QLatin1String(".tmp"))) continue;
                const QString path = s.folder + QLatin1Char('/') + b;
                run.read(path);
                bool ok = false;
                const QJsonObject root = readJsonObject(path, &ok);
                if (!ok) continue;
                const LegacyValues bv = legacyValues(SessionDiagnosticsFiles::legacyGolferInputOf(root));
                const auto add = [&](const QString &field, const QString &value, const QString &liveValue, const QJsonValue &json) {
                    RecoveredInput r{ b, field, value, liveValue, false, json };
                    for (const Decisions::Restore &d : run.decisions.restores)
                        if (d.session == sessionKey && d.from == b && d.field == field) r.restore = true;
                    // One case per field and value: the newest backup holding it is named (they sort by tag).
                    for (const RecoveredInput &x : std::as_const(g.recovered))
                        if (x.field == field && x.value == value && !r.restore) return;
                    g.recovered.append(r);
                };
                if (!bv.focus.conditionId.isEmpty() && before.focus.conditionId.isEmpty())
                    add(QStringLiteral("declaredFocus"), describeFocus(bv.focus), {}, focusJson(bv.focus));
                if (!bv.miss.isEmpty() && before.declaredMiss.isEmpty())
                    add(QStringLiteral("declaredMiss"), bv.miss, {}, bv.miss);
                if (!bv.screens.isEmpty() && before.screens.isEmpty())
                    add(QStringLiteral("screenAnswers"), describeScreens(bv.screens), {}, screensJson(bv.screens));
                if (!bv.run.shape.isEmpty() && before.intentRun.shape.isEmpty())
                    add(QStringLiteral("intentRuns"), describeRun(bv.run), {}, runsJson(bv.run));
                for (auto it = bv.shotIntent.constBegin(); it != bv.shotIntent.constEnd(); ++it)
                    if (!before.shotIntent.contains(it.key()))
                        add(QStringLiteral("shotIntent:%1").arg(it.key()), it.value(), {}, it.value());
                for (auto it = bv.corrections.constBegin(); it != bv.corrections.constEnd(); ++it)
                    if (!before.corrections.contains(it.key()))
                        add(QStringLiteral("labelCorrections:%1").arg(it.key()), describeCorrections(it.value()), {}, correctionsJson(it.value()));
            }
            // A restore Mark asked for: into its new home, over whatever the new home holds.
            for (const RecoveredInput &r : std::as_const(g.recovered)) {
                if (!r.restore) continue;
                g.restoring = true;
                if (r.field.contains(QLatin1Char(':'))) {
                    const int n = r.field.section(QLatin1Char(':'), 1).toInt();
                    if (!hasSwing(s, n)) continue;
                    g.reviewEdits[n].insert(r.field.startsWith(QLatin1String("shotIntent")) ? QStringLiteral("shotIntent")
                                                                                            : QStringLiteral("labelCorrections"), r.json);
                } else {
                    if (!sessionMoves && fields.isEmpty()) {
                        if (!sin.input.declaredFocus) fields.insert(QStringLiteral("declaredFocus"), focusJson(before.focus));
                        if (!sin.input.declaredMiss)  fields.insert(QStringLiteral("declaredMiss"), before.declaredMiss);
                        if (!sin.input.screenAnswers) fields.insert(QStringLiteral("screenAnswers"), screensJson(before.screens));
                        if (!sin.input.intentRuns)    fields.insert(QStringLiteral("intentRuns"), runsJson(before.intentRun));
                    }
                    fields.insert(r.field, r.json);
                }
            }
            g.sessionInputFields = fields;
            g.writeSessionInput  = !fields.isEmpty();

            if (g.legacyKeys.isEmpty() && g.recovered.isEmpty()) continue;   // nothing of the golfer's here
            if (g.skipped) { g.reviewEdits.clear(); g.writeSessionInput = false; g.keysToRemove.clear(); }
            if (!g.skipped && (g.writeSessionInput || !g.reviewEdits.isEmpty())) athleteTouched = true;
            run.res.golfer.append(g);
        }
        if (athleteTouched && QFileInfo::exists(AthleteFiles::tendenciesFile(a.folder))) run.res.tendenciesToDrop << a.folder;
    }
}

// ── M4 ───────────────────────────────────────────────────────────────────────────────────────────────

void planWrist(Run &run)
{
    WristPlan &w = run.res.wrist;
    w.settingValue = run.wristSetting;
    if (w.settingValue.trimmed().isEmpty()) {
        w.outcome = WristPlan::Outcome::NotSet;
        w.why = run.haveSettings ? QStringLiteral("ui/wristReferenceSwingDir is not set on this machine") : QStringLiteral("no settings file given");
        return;
    }
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(w.settingValue));
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const int number = parts.isEmpty() ? 0 : SwingPaths::swingNumberOf(parts.last());
    if (parts.size() < 3 || number < 1) {
        w.outcome = WristPlan::Outcome::NotFound;
        w.why = QStringLiteral("not a swing folder of a session");
        return;
    }
    w.athleteToken = parts.at(parts.size() - 3);
    const QString session = parts.at(parts.size() - 2);
    const SessionPlan *sp = sessionPlan(run.res, w.athleteToken, session);
    const QString here = run.library + QLatin1Char('/') + w.athleteToken + QLatin1Char('/') + session + QLatin1Char('/') + parts.last();
    if (!sp || !SwingStore::hasDocument(here)) {
        w.outcome = WristPlan::Outcome::NotFound;
        w.why = QStringLiteral("no such swing in this library (%1)").arg(here);
        return;
    }
    const AthletePlan *ap = athletePlan(run.res, w.athleteToken);
    if (!settles(ap) || sp->id.isNull() || sp->ownerProvisional || sp->ownerFromHeaders) {
        w.outcome = WristPlan::Outcome::Waits;
        w.why = QStringLiteral("waits for M1/M2: the athlete or the session has no settled id yet");
        w.shot = ShotRef(sp->id, number);
        return;
    }
    w.shot = ShotRef(sp->id, number);
    const ShotRef held = ShotRef::fromString(ap->record.value(QStringLiteral("wristReference")).toString());
    if (held == w.shot) w.outcome = WristPlan::Outcome::AlreadyThere;
    else if (!held.isNull()) {
        w.outcome = WristPlan::Outcome::RecordHasOther;
        w.why = QStringLiteral("the record already holds %1: never overwritten (2.3's J8)").arg(held.toString());
    } else w.outcome = WristPlan::Outcome::Write;
}

// ── Writing ──────────────────────────────────────────────────────────────────────────────────────────

struct Backup {
    QString source, copy;
    QByteArray sha;
    qint64 bytes = 0;
};

class Writer
{
public:
    explicit Writer(Run &run) : m_run(run), m_log(run.res.log) {}

    bool backupAll(const QStringList &files, bool settings);
    void writeAthletes();
    void writeSessions();
    void writeGolferInput();
    void writeWrist();
    void changeSettings();

private:
    bool copyOne(const QString &source, const QString &copy);
    bool backupTargetAllowed(const QString &target) const;
    QString backupPathOf(const QString &source) const;
    WriteResult writeLocked(const QString &folder, const QString &file, const QByteArray &bytes, bool makeFolder, bool mirror = false);
    bool targetAllowed(const QString &target, bool mirror, QString *why) const;
    QJsonObject athleteFileFor(const AthletePlan &p) const;
    void fail(const QString &what) { m_log.failures << what; }

    Run      &m_run;
    WriteLog &m_log;
    QList<Backup> m_backups;
    QSet<QString> m_athletesWritten;      // tokens whose athlete.json was written and read back (or already held)
};

QString Writer::backupPathOf(const QString &source) const
{
    const QString s = clean(source);
    const QString lib = clean(m_run.library);
    if (s.startsWith(lib + QLatin1Char('/')))
        return m_log.backupRoot + QStringLiteral("/library/") + s.mid(lib.size() + 1);
    if (!m_run.opt.settingsFile.isEmpty() && s == clean(m_run.opt.settingsFile))
        return m_log.backupRoot + QStringLiteral("/settings/") + QFileInfo(s).fileName();
    QString rest = s;
    rest.remove(QLatin1Char(':'));
    return m_log.backupRoot + QStringLiteral("/archive/") + rest;   // the archive mirror's session.json
}

bool Writer::backupTargetAllowed(const QString &target) const
{
    if (!isUnder(target, { m_run.opt.backupDir })) return false;
    return m_run.opt.approved || isUnder(target, m_run.opt.scratchRoots);
}

bool Writer::copyOne(const QString &source, const QString &copy)
{
    if (!backupTargetAllowed(copy)) return false;
    if (!QDir().mkpath(QFileInfo(copy).absolutePath())) return false;
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = in.readAll();
    in.close();
    const AtomicWriteResult w = atomicWrite(copy, bytes);
    if (!w.ok) return false;
    bool ok = false;
    const QByteArray again = sha256Of(copy, &ok);
    const QByteArray want = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    if (!ok || again != want) return false;
    m_backups.append({ clean(source), copy, want, bytes.size() });
    return true;
}

bool Writer::backupAll(const QStringList &files, bool settings)
{
    QStringList all = files;
    if (settings && !m_run.opt.settingsFile.isEmpty()) all.prepend(m_run.opt.settingsFile);
    all.removeDuplicates();
    for (const QString &f : std::as_const(all)) {
        if (!QFileInfo::exists(f)) continue;
        if (!copyOne(f, backupPathOf(f))) {
            fail(QStringLiteral("backup of %1 failed: nothing was written").arg(f));
            return false;
        }
    }
    QJsonArray entries;
    for (const Backup &b : std::as_const(m_backups))
        entries.append(QJsonObject{ { QStringLiteral("source"), b.source }, { QStringLiteral("backup"), b.copy },
                                    { QStringLiteral("sha256"), QString::fromLatin1(b.sha) }, { QStringLiteral("bytes"), b.bytes } });
    const QJsonObject manifest{ { QStringLiteral("tool"), QLatin1String(kToolName) }, { QStringLiteral("version"), QLatin1String(kToolVersion) },
                                { QStringLiteral("at"), isoUtc(m_run.res.at) }, { QStringLiteral("machine"), m_run.res.machine },
                                { QStringLiteral("library"), m_run.library }, { QStringLiteral("files"), entries } };
    const QString manifestFile = m_log.backupRoot + QStringLiteral("/manifest.json");
    if (!backupTargetAllowed(manifestFile)
        || !atomicWrite(manifestFile, QJsonDocument(manifest).toJson(QJsonDocument::Indented)).ok) {
        fail(QStringLiteral("the backup manifest could not be written: nothing was written"));
        return false;
    }
    return true;
}

bool Writer::targetAllowed(const QString &target, bool mirror, QString *why) const
{
    // EVERY WRITE TARGET, RESOLVED (review 1, finding 3): a library write lands inside the library's real folder; any
    // write without Mark's approval lands under a real scratch root. A symlink pointing elsewhere is refused here.
    if (!mirror && !isUnder(target, { m_run.library })) {
        *why = QStringLiteral("%1 resolves to %2, outside the library %3: not written").arg(target, resolvedPath(target), resolvedPath(m_run.library));
        return false;
    }
    if (!m_run.opt.approved && !isUnder(target, m_run.opt.scratchRoots)) {
        *why = QStringLiteral("%1 resolves to %2, outside the scratch roots: not written without --i-have-marks-approval")
                   .arg(target, resolvedPath(target));
        return false;
    }
    return true;
}

WriteResult Writer::writeLocked(const QString &folder, const QString &file, const QByteArray &bytes, bool makeFolder, bool mirror)
{
    QString why;
    if (!targetAllowed(file, mirror, &why)) return { WriteResult::Status::Failed, why };
    if (makeFolder && !QDir().mkpath(folder))
        return { WriteResult::Status::Failed, QStringLiteral("the folder %1 could not be made").arg(folder) };
    FolderLock lock(folder);
    switch (lock.lock()) {
    case FolderLock::Result::Acquired: break;
    case FolderLock::Result::Locked:
        return { WriteResult::Status::Locked, QStringLiteral("%1 is locked by %2").arg(folder, lock.holder()) };
    case FolderLock::Result::Failed:
        return { WriteResult::Status::Failed, lock.errorString() };
    }
    const AtomicWriteResult w = atomicWrite(file, bytes, AtomicWriteOptions::intoExistingFolder());
    if (!w.ok) return { WriteResult::Status::Failed, w.error + QStringLiteral(" ") + w.detail };
    m_log.written << clean(file);
    return { WriteResult::Status::Written, {} };
}

QJsonObject Writer::athleteFileFor(const AthletePlan &p) const
{
    QJsonObject file;
    if (const AthleteScan *scan = [&]() -> const AthleteScan * {
            for (const AthleteScan &a : m_run.scans) if (a.token == p.token) return &a;
            return nullptr; }(); scan && scan->file == AthleteScan::File::Read)
        file = scan->json;   // unknown top-level keys kept
    file.insert(QStringLiteral("schema"), QLatin1String(AthleteStore::kSchema));
    file.insert(QStringLiteral("athleteId"), p.id.toString());
    file.insert(QStringLiteral("record"), p.record);
    QJsonArray groups;
    for (const QString &g : p.settingsGroupsToRemove) groups.append(g);
    QJsonObject from{ { QStringLiteral("tool"), QLatin1String(kToolName) }, { QStringLiteral("version"), QLatin1String(kToolVersion) },
                      { QStringLiteral("at"), isoUtc(m_run.res.at) }, { QStringLiteral("machine"), m_run.res.machine },
                      { QStringLiteral("settingsFile"), m_run.opt.settingsFile }, { QStringLiteral("settingsGroups"), groups } };
    if (!m_run.res.otherMachine.isEmpty()) from.insert(QStringLiteral("otherMachine"), m_run.res.otherMachine);
    if (!p.differences.isEmpty()) {
        QJsonObject decided;
        for (const FieldDifference &d : p.differences) decided.insert(d.key, d.chosen.isUndefined() ? QJsonValue() : d.chosen);
        from.insert(QStringLiteral("decided"), decided);
    }
    file.insert(QStringLiteral("migratedFrom"), from);
    return file;
}

void Writer::writeAthletes()
{
    for (const AthletePlan &p : std::as_const(m_run.res.athletes)) {
        if (p.outcome == AthletePlan::Outcome::AlreadyInLibrary) { m_athletesWritten.insert(p.token); continue; }
        if (p.outcome != AthletePlan::Outcome::WriteNew && p.outcome != AthletePlan::Outcome::Update) continue;
        const QJsonObject file = athleteFileFor(p);
        const QString path = AthleteFiles::athleteFile(p.folder);
        const WriteResult w = writeLocked(p.folder, path, QJsonDocument(file).toJson(QJsonDocument::Indented), true);
        if (!w.ok()) { fail(QStringLiteral("M1 %1: athlete.json not written (%2); its settings record kept").arg(p.token, w.error)); continue; }
        // The read-back: the id and every field, as athlete.json now holds them.
        bool ok = false;
        const QJsonObject back = readJsonObject(path, &ok);
        if (!ok || AthleteId::fromString(back.value(QStringLiteral("athleteId")).toString()) != p.id
            || storedRecord(back.value(QStringLiteral("record")).toObject().toVariantMap()) != p.record) {
            fail(QStringLiteral("M1 %1: athlete.json did not read back as written; its settings record kept").arg(p.token));
            continue;
        }
        m_athletesWritten.insert(p.token);
    }
}

void Writer::changeSettings()
{
    if (m_run.opt.settingsFile.isEmpty()) return;
    QStringList remove;
    QMap<QString, QString> rekey;   // old id → new id
    for (const AthletePlan &p : std::as_const(m_run.res.athletes)) {
        if (!m_athletesWritten.contains(p.token)) continue;
        remove << p.settingsGroupsToRemove;
        if (!p.thisMachine.isEmpty() && p.thisMachine.first().id != p.id)
            rekey.insert(p.thisMachine.first().id.toString(), p.id.toString());
    }
    if (remove.isEmpty() && rekey.isEmpty()) return;
    if (!m_run.opt.approved && !isUnder(m_run.opt.settingsFile, m_run.opt.scratchRoots)) {
        fail(QStringLiteral("the settings file %1 resolves to %2, outside the scratch roots: not written")
                 .arg(m_run.opt.settingsFile, resolvedPath(m_run.opt.settingsFile)));
        return;
    }
    {
        QSettings s(m_run.opt.settingsFile, QSettings::IniFormat);
        for (const QString &g : std::as_const(remove)) {
            s.remove(QStringLiteral("athletes/") + g);
            m_log.settingsChanges << QStringLiteral("removed athletes/%1").arg(g);
        }
        const QString current = s.value(QStringLiteral("currentAthleteUuid")).toString();
        for (auto it = rekey.constBegin(); it != rekey.constEnd(); ++it) {
            if (current.compare(it.key(), Qt::CaseInsensitive) == 0) {
                s.setValue(QStringLiteral("currentAthleteUuid"), it.value());
                m_log.settingsChanges << QStringLiteral("currentAthleteUuid %1 → %2").arg(it.key(), it.value());
            }
            // The learned club lengths: re-keyed here only on a machine whose Rig has not taken them (run() refuses
            // the write run otherwise: Options::rigFolder).
            QVariantMap prior = s.value(QStringLiteral("analysis/clubLenPrior")).toMap();
            bool changed = false;
            for (const QString &k : prior.keys())
                if (k.startsWith(it.key() + QLatin1Char('|'), Qt::CaseInsensitive)) {
                    const QString nk = it.value() + k.mid(it.key().size());
                    prior.insert(nk, prior.take(k));
                    m_log.settingsChanges << QStringLiteral("analysis/clubLenPrior \"%1\" → \"%2\"").arg(k, nk);
                    changed = true;
                }
            if (changed) s.setValue(QStringLiteral("analysis/clubLenPrior"), prior);
        }
        s.sync();
        if (s.status() != QSettings::NoError) { fail(QStringLiteral("the settings file could not be written")); return; }
    }
    // The read-back: the groups are gone.
    QSettings check(m_run.opt.settingsFile, QSettings::IniFormat);
    check.beginGroup(QStringLiteral("athletes"));
    const QStringList left = check.childGroups();
    for (const QString &g : std::as_const(remove))
        if (left.contains(g)) fail(QStringLiteral("the settings group athletes/%1 is still there after the write").arg(g));
}

void Writer::writeSessions()
{
    for (const SessionPlan &p : std::as_const(m_run.res.sessions)) {
        if (p.outcome != SessionPlan::Outcome::Write && p.outcome != SessionPlan::Outcome::CorrectStart) continue;
        if (p.outcome == SessionPlan::Outcome::Write && !m_athletesWritten.contains(p.athleteToken)) {
            fail(QStringLiteral("M2 %1/%2: skipped, its athlete's record was not written (M1)").arg(p.athleteToken, p.name));
            continue;
        }
        QJsonObject o;
        QString read;
        if (p.outcome == SessionPlan::Outcome::CorrectStart) {
            for (const AthleteScan &a : m_run.scans)
                for (const SessionScan &s : a.sessions) if (s.folder == p.folder) o = s.json;   // unknown keys kept
            read = QStringLiteral("the swings' capture times (clock.wallclock: the start), replacing the start the app inferred "
                                  "from folder times (%1); the rest as the app wrote it").arg(isoUtc(p.previousStartUtc));
        } else {
            o.insert(QStringLiteral("sessionId"), p.id.toString());
            o.insert(QStringLiteral("athleteId"), p.owner.toString());
            if (!p.type.isEmpty()) o.insert(QStringLiteral("type"), p.type);
            o.insert(QStringLiteral("status"), QStringLiteral("ended"));
            // The end is inferred, never observed: the latest swing's capture end, marked so (J-18: Mark,
            // 15 Nov 2026 — the app's rule for a session a crash left open, the same here; FILE-SESS-01).
            if (p.endUtc.isValid()) {
                o.insert(QStringLiteral("endedAt"), isoUtc(p.endUtc));
                o.insert(QStringLiteral("endInferred"), true);
            }
            read = p.timesFromFolderDate
                ? QStringLiteral("the folder's name (the id, D3, from the owning athlete's id; the start: its date at local midnight; "
                                 "no end); no swing")
                : QStringLiteral("the folder's name (the id, D3, from the owning athlete's id); the swings' capture times "
                                 "(clock.wallclock: the start; the latest window's end: the end, marked endInferred); the shots' capture.sessionType "
                                 "(the type)%1").arg(p.typeNeedsDecision ? QStringLiteral(", chosen by Mark") : QString());
        }
        o.insert(QStringLiteral("schema"), QLatin1String(SessionStore::kSchema));
        o.insert(QStringLiteral("startedAt"), isoUtc(p.startUtc));
        o.insert(QStringLiteral("origin"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("backfilled") },
                                                        { QStringLiteral("tool"), QLatin1String(kToolName) },
                                                        { QStringLiteral("version"), QLatin1String(kToolVersion) },
                                                        { QStringLiteral("at"), isoUtc(m_run.res.at) },
                                                        { QStringLiteral("read"), read } });
        const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Indented);
        const WriteResult w = writeLocked(p.folder, SessionFiles::sessionFile(p.folder), bytes, false);
        if (!w.ok()) { fail(QStringLiteral("M2 %1/%2: session.json not written (%3)").arg(p.athleteToken, p.name, w.error)); continue; }
        // The read-back, through the session repository as the app reads it.
        SessionStore reader;
        const SessionId id = reader.openFolder(p.folder);
        const auto e = reader.open(id);
        if (id != p.id || !e || !e->hasSessionFile || e->startedAtUtc != p.startUtc || e->status != SessionStatus::Ended
            || (p.outcome == SessionPlan::Outcome::Write && e->endInferred != p.endUtc.isValid())
            || e->identity.type() != (p.outcome == SessionPlan::Outcome::Write ? p.type : e->identity.type())) {
            fail(QStringLiteral("M2 %1/%2: session.json did not read back as written").arg(p.athleteToken, p.name));
            continue;
        }
        // The archive mirror holds the same file (the same id).
        if (p.archived && !p.mirrorFolder.isEmpty() && QFileInfo(p.mirrorFolder).isDir()) {
            const WriteResult m = writeLocked(p.mirrorFolder, SessionFiles::sessionFile(p.mirrorFolder), bytes, false, true);
            if (!m.ok()) fail(QStringLiteral("M2 %1/%2: the archive mirror's session.json not written (%3)").arg(p.athleteToken, p.name, m.error));
        }
    }
}

void Writer::writeGolferInput()
{
    QSet<QString> touchedAthletes;
    for (const GolferPlan &g : std::as_const(m_run.res.golfer)) {
        if (g.skipped) continue;
        {
            // EVERY FILE M3 MAY WRITE FOR THIS SESSION, resolved, before any of them is written (review 2):
            // diagnostics.json, session_input.json, and each swing document editReview rewrites (swing.ppsw, and the
            // JSON-era swing.json / swing_summary.json it removes). One refused: nothing is written for the session,
            // and its older keys stay.
            QStringList targets{ SessionFiles::diagnosticsFile(g.folder), SessionFiles::sessionInputFile(g.folder) };
            for (auto it = g.reviewEdits.constBegin(); it != g.reviewEdits.constEnd(); ++it) {
                const QString dir = g.folder + QLatin1Char('/') + SwingPaths::swingFolderName(it.key());
                targets << SwingStore::ppswPath(dir) << SwingStore::jsonPath(dir) << SwingStore::legacySummaryPath(dir);
            }
            QStringList refused;
            for (const QString &t : std::as_const(targets)) {
                QString why;
                if (!targetAllowed(t, false, &why)) refused << why;
            }
            if (!refused.isEmpty()) {
                fail(QStringLiteral("M3 %1/%2: nothing written for this session, its older keys kept: %3")
                         .arg(g.athleteToken, g.name, refused.join(QStringLiteral("; "))));
                continue;
            }
        }
        const SessionInputRead sinBefore = SessionDiagnosticsFiles::readSessionInput(g.folder);
        const DiagnosticsRead d = SessionDiagnosticsFiles::readDiagnostics(g.folder);
        const QJsonObject legacy = d.parsed() ? d.envelope.legacyGolferInput : QJsonObject();
        const GolferInput before = SessionDiagnosticsFiles::golferInput(g.folder, legacy, m_run.recordings, &sinBefore);
        bool ok = true;

        if (g.writeSessionInput) {
            const WriteResult w = SessionDiagnosticsFiles::writeSessionInput(g.folder, sessionInputFrom(g.sessionInputFields));
            if (w.ok()) m_log.written << clean(SessionFiles::sessionInputFile(g.folder));
            else { ok = false; fail(QStringLiteral("M3 %1/%2: session_input.json not written (%3)").arg(g.athleteToken, g.name, w.error)); }
        }
        for (auto it = g.reviewEdits.constBegin(); ok && it != g.reviewEdits.constEnd(); ++it) {
            const QString swingDir = g.folder + QLatin1Char('/') + SwingPaths::swingFolderName(it.key());
            const ShotRef shot = m_run.recordings.shotOfFolder(swingDir);
            ReviewEdit edit;
            if (it->contains(QStringLiteral("shotIntent"))) edit.shotIntent = it->value(QStringLiteral("shotIntent")).toString();
            if (it->contains(QStringLiteral("labelCorrections"))) {
                QVector<LabelCorrection> list;
                for (const QJsonValue &v : it->value(QStringLiteral("labelCorrections")).toArray()) {
                    const QJsonObject c = v.toObject();
                    list.append({ c.value(QStringLiteral("family")).toString(), c.value(QStringLiteral("from")).toString(),
                                  c.value(QStringLiteral("to")).toString() });
                }
                edit.labelCorrections = list;
            }
            const WriteResult w = m_run.recordings.editReview(shot, edit);
            if (w.ok() && w.status != WriteResult::Status::Unchanged) m_log.written << clean(SwingStore::info(swingDir).path);
            else { ok = false; fail(QStringLiteral("M3 %1/%2: %3's review not written (%4)").arg(g.athleteToken, g.name,
                                                                                               SwingPaths::swingFolderName(it.key()), w.error)); }
        }
        if (!ok) continue;   // a new home not written: diagnostics.json and its older keys stay exactly as they are
        if (g.writeSessionInput || !g.reviewEdits.isEmpty()) touchedAthletes.insert(g.athleteToken);
        if (m_run.opt.afterNewHomesForTest) m_run.opt.afterNewHomesForTest(g.folder);

        // THE CHECK (04 step 2): every older key's value is at its target. What the golfer's input resolves to
        // without the keys must be what it resolved to with them (plus any restore Mark asked for).
        if (g.keysToRemove.isEmpty() || !d.parsed()) continue;
        QJsonObject remaining = legacy;
        for (const QString &k : g.keysToRemove) remaining.remove(k);
        SessionStore                  checkSessions;
        SwingStoreRecordingRepository checkDoor(checkSessions);
        const GolferInput after = SessionDiagnosticsFiles::golferInput(g.folder, remaining, checkDoor);
        QString expected = describe(before);
        if (g.restoring) {
            const GolferInput withAll = SessionDiagnosticsFiles::golferInput(g.folder, legacy, checkDoor);
            expected = describe(withAll);   // the restored fields sit in their new homes, which win
        }
        if (describe(after) != expected) {
            fail(QStringLiteral("M3 %1/%2: the check failed — without the older keys the input reads [%3], with them [%4]; "
                                "diagnostics.json left as it was").arg(g.athleteToken, g.name, describe(after), expected));
            continue;
        }
        // The older keys out of diagnostics.json: the file as it stands, those keys removed, nothing else changed.
        bool parsed = false;
        QJsonObject root = readJsonObject(SessionFiles::diagnosticsFile(g.folder), &parsed);
        if (!parsed) { fail(QStringLiteral("M3 %1/%2: diagnostics.json could not be re-read").arg(g.athleteToken, g.name)); continue; }
        for (const QString &k : g.keysToRemove) root.remove(k);
        const WriteResult w = session_diagnostics_detail::writeJson(g.folder, SessionFiles::diagnosticsFile(g.folder), root,
                                                                    QJsonDocument::Compact);
        if (w.ok()) m_log.written << clean(SessionFiles::diagnosticsFile(g.folder));
        else fail(QStringLiteral("M3 %1/%2: diagnostics.json not rewritten (%3)").arg(g.athleteToken, g.name, w.error));
    }
    // J-45: tendencies.json is a cache the shots' reviews feed; with the input moved it is rebuilt from the new homes.
    for (const QString &folder : std::as_const(m_run.res.tendenciesToDrop)) {
        if (!touchedAthletes.contains(QFileInfo(folder).fileName())) continue;
        const QString f = AthleteFiles::tendenciesFile(folder);
        QString why;
        if (!targetAllowed(f, false, &why)) { fail(QStringLiteral("M3: %1").arg(why)); continue; }
        if (QFile::remove(f)) m_log.removed << clean(f);
        else fail(QStringLiteral("M3: %1 could not be removed").arg(f));
    }
}

void Writer::writeWrist()
{
    const WristPlan &w = m_run.res.wrist;
    if (w.outcome != WristPlan::Outcome::Write) return;
    const AthletePlan *ap = athletePlan(m_run.res, w.athleteToken);
    if (!ap || !m_athletesWritten.contains(ap->token)) { fail(QStringLiteral("M4: the athlete's record was not written (M1)")); return; }
    const QString path = AthleteFiles::athleteFile(ap->folder);
    bool ok = false;
    QJsonObject file = readJsonObject(path, &ok);
    if (!ok) { fail(QStringLiteral("M4: %1 could not be read").arg(path)); return; }
    QJsonObject record = file.value(QStringLiteral("record")).toObject();
    record.insert(QStringLiteral("wristReference"), w.shot.toString());
    file.insert(QStringLiteral("record"), record);
    const WriteResult r = writeLocked(ap->folder, path, QJsonDocument(file).toJson(QJsonDocument::Indented), false);
    if (!r.ok()) { fail(QStringLiteral("M4: athlete.json not written (%1)").arg(r.error)); return; }
    const QJsonObject back = readJsonObject(path, &ok);
    if (!ok || ShotRef::fromString(back.value(QStringLiteral("record")).toObject().value(QStringLiteral("wristReference")).toString()) != w.shot)
        fail(QStringLiteral("M4: the reference did not read back"));
}

} // namespace

// ── Public ───────────────────────────────────────────────────────────────────────────────────────────

QString resolvedPath(const QString &path)
{
    // The real path: every symlink resolved (on macOS /mnt resolves into /System/Volumes/Data, for a path and a root
    // alike), and on Windows every directory junction too (realPath, #243). A path not there yet resolves through its
    // nearest existing folder, the rest appended.
    QString     p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList rest;
    while (!p.isEmpty()) {
        const QString canon = realPath(p);
        if (!canon.isEmpty()) {
            QString out = QDir::cleanPath(canon);
            for (auto it = rest.crbegin(); it != rest.crend(); ++it) out += QLatin1Char('/') + *it;
            return QDir::cleanPath(out);
        }
        const QFileInfo fi(p);
        if (fi.fileName().isEmpty() || fi.path() == p) break;
        rest << fi.fileName();
        p = fi.path();
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isUnder(const QString &path, const QStringList &roots)
{
    // RESOLVED PATHS ONLY (review 1, finding 3): a symlink under a root that points elsewhere is not under it.
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString p = resolvedPath(path);
    for (const QString &root : roots) {
        if (root.trimmed().isEmpty()) continue;
        const QString r = resolvedPath(root);
        if (p.compare(r, cs) == 0 || p.startsWith(r + QLatin1Char('/'), cs)) return true;
    }
    return false;
}

bool outputAllowed(const Result &result, const QString &path)
{
    const QString library = resolvedPath(result.options.library);
    const QString p = resolvedPath(path);
    if (p.compare(library, Qt::CaseInsensitive) == 0) return false;
    if (QFileInfo(p).path().compare(library, Qt::CaseInsensitive) == 0) return false;
    QStringList folders;
    for (const AthletePlan &a : result.athletes) folders << a.folder;
    for (const SessionPlan &s : result.sessions) folders << s.folder;
    return !isUnder(p, folders);
}

QList<RecordSource> readSettingsRecords(const QString &iniPath, const QString &origin, QString *librarySetting,
                                        QString *wristSetting, QString *currentAthlete, QStringList *priorKeys, QString *error)
{
    QList<RecordSource> out;
    // A private copy: QSettings never opens the real file, so a read cannot touch it (nor leave a lock file).
    QTemporaryDir tmp;
    QFile in(iniPath);
    if (!tmp.isValid() || !in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("the settings file %1 cannot be read").arg(iniPath);
        return out;
    }
    const QString copy = tmp.filePath(QStringLiteral("settings.ini"));
    {
        QFile c(copy);
        if (!c.open(QIODevice::WriteOnly) || c.write(in.readAll()) < 0) {
            if (error) *error = QStringLiteral("the settings file could not be copied for reading");
            return out;
        }
    }
    QSettings s(copy, QSettings::IniFormat);
    if (librarySetting) *librarySetting = s.value(QStringLiteral("General/athleteLibraryPath")).toString();
    if (wristSetting) *wristSetting = s.value(QStringLiteral("ui/wristReferenceSwingDir")).toString();
    if (currentAthlete) *currentAthlete = s.value(QStringLiteral("currentAthleteUuid")).toString();
    if (priorKeys) *priorKeys = s.value(QStringLiteral("analysis/clubLenPrior")).toMap().keys();
    s.beginGroup(QStringLiteral("athletes"));
    for (const QString &key : s.childGroups()) {
        const AthleteId id = AthleteId::fromString(key);
        if (id.isNull()) continue;
        s.beginGroup(key);
        QVariantMap m;
        for (const QString &k : s.childKeys()) m.insert(k, s.value(k));
        s.endGroup();
        RecordSource r;
        r.origin = origin;
        r.id = id;
        r.settingsKey = key;
        r.record = storedRecord(m);
        out.append(r);
    }
    s.endGroup();
    return out;
}

int Result::writesPlanned() const
{
    int n = 0;
    for (const AthletePlan &p : athletes)
        if (p.outcome == AthletePlan::Outcome::WriteNew || p.outcome == AthletePlan::Outcome::Update) ++n;
    bool settings = false;
    for (const AthletePlan &p : athletes)
        if ((p.outcome == AthletePlan::Outcome::WriteNew || p.outcome == AthletePlan::Outcome::Update
             || p.outcome == AthletePlan::Outcome::AlreadyInLibrary) && !p.settingsGroupsToRemove.isEmpty()) settings = true;
    if (settings) ++n;
    for (const SessionPlan &s : sessions)
        if (s.outcome == SessionPlan::Outcome::Write || s.outcome == SessionPlan::Outcome::CorrectStart)
            n += 1 + (s.archived && !s.mirrorFolder.isEmpty() ? 1 : 0);
    for (const GolferPlan &g : golfer) {
        if (g.skipped) continue;
        n += (g.writeSessionInput ? 1 : 0) + int(g.reviewEdits.size()) + (g.keysToRemove.isEmpty() ? 0 : 1);
    }
    n += int(tendenciesToDrop.size());
    if (wrist.outcome == WristPlan::Outcome::Write) ++n;
    return n;
}

Result run(const Options &options)
{
    Result res;
    res.options = options;
    res.at      = QDateTime::currentDateTimeUtc();
    res.machine = options.machine.isEmpty() ? QSysInfo::machineHostName() : options.machine;
    Run run(res);
    run.opt     = options;
    run.library = clean(options.library);

    const auto refuse = [&](const QString &why) { res.refused = true; res.refusal = why; return res; };
    if (options.library.isEmpty() || !QFileInfo(run.library).isDir())
        return refuse(QStringLiteral("no such library folder: %1").arg(options.library));

    // The guards of a write (the dry run has none: it writes nothing).
    if (options.write) {
        if (options.backupDir.isEmpty())
            return refuse(QStringLiteral("--write needs --backup-dir: every file is copied there before it changes"));
        if (!options.approved) {
            if (!isUnder(run.library, options.scratchRoots))
                return refuse(QStringLiteral("--write on %1 refused: a write is allowed only on a copy under %2 without "
                                             "--i-have-marks-approval").arg(run.library, options.scratchRoots.join(QStringLiteral(" or "))));
            if (!isUnder(options.backupDir, options.scratchRoots))
                return refuse(QStringLiteral("the backups of a scratch run go under %1 too").arg(options.scratchRoots.join(QStringLiteral(" or "))));
            if (!options.settingsFile.isEmpty() && !isUnder(options.settingsFile, options.scratchRoots))
                return refuse(QStringLiteral("--write would change the settings file %1, which is not a copy under %2: a scratch run "
                                             "takes a copy of the settings").arg(options.settingsFile, options.scratchRoots.join(QStringLiteral(" or "))));
        }
        // The Rig has taken the learned club lengths (SET-05) from this machine's settings: M1's re-key of
        // analysis/clubLenPrior would change that group's settings keys, and the app's next launch would copy them,
        // as they stood before the Rig took them, over every length learned since. So the whole write run is refused
        // (the Product Owner, 27 Nov 2026); a dry run still runs.
        if (!options.settingsFile.isEmpty() && options.rigFolder.isEmpty())
            return refuse(QStringLiteral("--write with a settings file needs the machine's rig folder (--rig-dir), to "
                                         "check that its Rig has not taken the learned club lengths"));
        if (!options.rigFolder.isEmpty() && RigStore::hasTakenGroup(options.rigFolder, RigStore::Group::ClubLengthPrior))
            return refuse(QStringLiteral("--write refused: the Rig in %1 keeps this machine's learned club lengths "
                                         "(rig.json has taken them from the settings), so re-keying the settings copy "
                                         "would undo every length learned since; a dry run still works")
                              .arg(options.rigFolder));
        const bool running = options.appRunning ? options.appRunning() : appIsRunning();
        if (running)
            return refuse(QStringLiteral("PinPoint Studio is running on this machine: quit it first (and on every other machine "
                                         "that has the library open)"));
    }

    QString err;
    if (!loadDecisions(run, &err)) return refuse(QStringLiteral("the decisions file: %1").arg(err));
    if (!loadOther(run, &err)) return refuse(QStringLiteral("the other machine's export: %1").arg(err));
    if (!options.settingsFile.isEmpty()) {
        run.read(options.settingsFile);
        run.mine = readSettingsRecords(options.settingsFile, thisOrigin(run), &res.libraryInSettings, &run.wristSetting,
                                       &run.currentAthlete, &run.priorKeys, &err);
        if (!err.isEmpty()) return refuse(err);
        run.haveSettings = true;
    }

    // The library: every folder that holds sessions or an athlete.json, and every folder a record names.
    QSet<QString> tokens;
    for (const QString &name : QDir(run.library).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name.startsWith(QLatin1Char('.'))) continue;   // the library's trash, and other hidden folders
        AthleteScan a = scanAthlete(run, name);
        if (a.sessions.isEmpty() && a.file == AthleteScan::File::Missing) { res.notAthleteFolders << name; continue; }
        tokens.insert(name);
        run.scans.append(a);
    }
    for (const QList<RecordSource> *list : { &run.mine, &run.other })
        for (const RecordSource &r : *list) {
            const QString name = recordName(r.record);
            const QString token = SwingPaths::sanitise(!name.trimmed().isEmpty() ? name : r.id.toString());
            if (!tokens.contains(token)) {
                tokens.insert(token);
                res.notAthleteFolders.removeAll(token);
                run.scans.append(scanAthlete(run, token));
            }
        }

    // Locks (R-PERS-6): a write refuses while any is held.
    for (const AthleteScan &a : std::as_const(run.scans)) {
        if (QFileInfo::exists(a.folder + QLatin1Char('/') + FolderLock::fileName())) res.locksHeld << a.folder + QLatin1Char('/') + FolderLock::fileName();
        for (const SessionScan &s : a.sessions) {
            if (QFileInfo::exists(s.folder + QLatin1Char('/') + FolderLock::fileName())) res.locksHeld << s.folder + QLatin1Char('/') + FolderLock::fileName();
            if (s.archived && !s.mirror.isEmpty() && QFileInfo::exists(s.mirror + QLatin1Char('/') + FolderLock::fileName()))
                res.locksHeld << s.mirror + QLatin1Char('/') + FolderLock::fileName();   // the archive mirror's too
        }
    }

    if (!res.libraryInSettings.isEmpty() && resolvedPath(res.libraryInSettings) != resolvedPath(run.library))
        res.warnings << QStringLiteral("The settings' library (%1) is not the --library given (%2): check that this is the "
                                       "library meant (a scratch copy of the settings still names the live one).")
                            .arg(res.libraryInSettings, run.library);
    planAthletes(run);
    planSessions(run);
    planGolferInput(run);
    planWrist(run);
    res.filesRead = QStringList(run.reads.cbegin(), run.reads.cend());
    std::sort(res.filesRead.begin(), res.filesRead.end());

    for (const QString &out : options.outputs)
        if (!outputAllowed(res, out)) {
            res.outputRefused = true;
            return refuse(QStringLiteral("%1 may not be inside an athlete or session folder, nor directly in the library's root")
                              .arg(out));
        }
    if (!options.write) return res;

    // ── THE WRITE ────────────────────────────────────────────────────────────────────────────────────
    if (!outputAllowed(res, options.backupDir))
        return refuse(QStringLiteral("the backup folder %1 may not be inside an athlete or session folder, nor directly in the "
                                     "library's root").arg(options.backupDir));
    if (!res.locksHeld.isEmpty())
        return refuse(QStringLiteral("%1 folder lock(s) are held in the library (another PinPoint writer is at work, or one "
                                     "crashed): nothing written").arg(res.locksHeld.size()));
    if (!res.inProgress.isEmpty() && !options.allowInProgress)
        return refuse(QStringLiteral("%1 session(s) are in progress (D11): nothing written. A session a crash left open can be "
                                     "passed over with --allow-in-progress").arg(res.inProgress.size()));
    // THE ARCHIVE MIRROR (review 1, finding 2): it lies outside the library, where archived.json says. Without
    // Mark's approval a write may touch it only when it, resolved, is under a scratch root too.
    for (const SessionPlan &s : std::as_const(res.sessions))
        if ((s.outcome == SessionPlan::Outcome::Write || s.outcome == SessionPlan::Outcome::CorrectStart) && s.archived
            && !s.mirrorFolder.isEmpty() && QFileInfo(s.mirrorFolder).isDir() && !options.approved
            && !isUnder(s.mirrorFolder, options.scratchRoots))
            return refuse(QStringLiteral("the archive mirror %1 of %2/%3 is not under %4: nothing written (a scratch copy of an "
                                         "archived session must have its mirror copied under scratch too)")
                              .arg(s.mirrorFolder, s.athleteToken, s.name, options.scratchRoots.join(QStringLiteral(" or "))));
    if (res.writesPlanned() == 0) return res;   // idempotent: nothing to do, so not even a backup is made

    // Backups first: every file the plan changes or removes, and the settings file.
    QStringList files;
    bool settingsChange = false;
    for (const AthletePlan &p : std::as_const(res.athletes)) {
        if (p.outcome == AthletePlan::Outcome::Update) files << AthleteFiles::athleteFile(p.folder);
        if (settles(&p) && !p.settingsGroupsToRemove.isEmpty()) settingsChange = true;
    }
    for (const SessionPlan &s : std::as_const(res.sessions)) {
        if (s.outcome == SessionPlan::Outcome::CorrectStart) files << SessionFiles::sessionFile(s.folder);
        if ((s.outcome == SessionPlan::Outcome::Write || s.outcome == SessionPlan::Outcome::CorrectStart) && s.archived
            && !s.mirrorFolder.isEmpty())
            files << SessionFiles::sessionFile(s.mirrorFolder);   // an existing mirror copy is backed up too
    }
    for (const GolferPlan &g : std::as_const(res.golfer)) {
        if (g.skipped) continue;
        if (g.writeSessionInput) files << SessionFiles::sessionInputFile(g.folder);
        if (!g.keysToRemove.isEmpty()) files << SessionFiles::diagnosticsFile(g.folder);
        for (auto it = g.reviewEdits.constBegin(); it != g.reviewEdits.constEnd(); ++it) {
            const QString dir = g.folder + QLatin1Char('/') + SwingPaths::swingFolderName(it.key());
            files << SwingStore::ppswPath(dir) << SwingStore::jsonPath(dir) << SwingStore::legacySummaryPath(dir);
        }
    }
    for (const QString &f : std::as_const(res.tendenciesToDrop)) files << AthleteFiles::tendenciesFile(f);
    if (res.wrist.outcome == WristPlan::Outcome::Write)
        if (const AthletePlan *ap = athletePlan(res, res.wrist.athleteToken)) files << AthleteFiles::athleteFile(ap->folder);

    const QString stem = QDir::cleanPath(options.backupDir) + QStringLiteral("/step2-backup-")
                       + res.at.toLocalTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    res.log.backupRoot = stem;
    for (int n = 2; QFileInfo::exists(res.log.backupRoot); ++n)   // a run's backups are never mixed with another's
        res.log.backupRoot = stem + QLatin1Char('-') + QString::number(n);
    Writer writer(run);
    if (!writer.backupAll(files, settingsChange)) return res;

    writer.writeAthletes();     // M1 (athlete.json, read back) …
    writer.writeSessions();     // M2
    writer.writeGolferInput();  // M3
    writer.writeWrist();        // M4
    writer.changeSettings();    // … and M1's settings records go last, once their athlete.json read back
    return res;
}

QJsonObject exportRecords(const Result &r)
{
    QJsonArray athletes;
    for (const AthletePlan &p : r.athletes)
        for (const RecordSource &s : p.thisMachine)
            athletes.append(QJsonObject{ { QStringLiteral("athleteId"), s.id.toString() }, { QStringLiteral("settingsKey"), s.settingsKey },
                                         { QStringLiteral("folder"), p.token }, { QStringLiteral("record"), s.record } });
    return QJsonObject{ { QStringLiteral("schema"), QLatin1String(kExportSchema) },
                        { QStringLiteral("tool"), QLatin1String(kToolName) }, { QStringLiteral("version"), QLatin1String(kToolVersion) },
                        { QStringLiteral("machine"), r.machine }, { QStringLiteral("exportedAt"), isoUtc(r.at) },
                        { QStringLiteral("settingsFile"), r.options.settingsFile }, { QStringLiteral("library"), r.options.library },
                        { QStringLiteral("libraryInSettings"), r.libraryInSettings },
                        { QStringLiteral("wristReferenceSwingDir"), r.wrist.settingValue },
                        { QStringLiteral("athletes"), athletes } };
}

// ── The report ───────────────────────────────────────────────────────────────────────────────────────

namespace {

QString outcomeWord(AthletePlan::Outcome o)
{
    switch (o) {
    case AthletePlan::Outcome::WriteNew:         return QStringLiteral("**write** athlete.json");
    case AthletePlan::Outcome::Update:           return QStringLiteral("**update** athlete.json");
    case AthletePlan::Outcome::AlreadyInLibrary: return QStringLiteral("already in the library");
    case AthletePlan::Outcome::NothingHere:      return QStringLiteral("nothing on this machine");
    case AthletePlan::Outcome::NoRecord:         return QStringLiteral("**no record**");
    case AthletePlan::Outcome::Blocked:          return QStringLiteral("**needs a decision**");
    }
    return {};
}

QString outcomeWord(SessionPlan::Outcome o)
{
    switch (o) {
    case SessionPlan::Outcome::Write:           return QStringLiteral("**write**");
    case SessionPlan::Outcome::CorrectStart:    return QStringLiteral("**correct start** (J-19)");
    case SessionPlan::Outcome::HasFile:         return QStringLiteral("has session.json");
    case SessionPlan::Outcome::AlreadyMigrated: return QStringLiteral("already migrated");
    case SessionPlan::Outcome::Skipped:         return QStringLiteral("skipped");
    }
    return {};
}

QString moveWord(GolferMove::Action a)
{
    switch (a) {
    case GolferMove::Action::Move:           return QStringLiteral("**move**");
    case GolferMove::Action::AlreadyThere:   return QStringLiteral("already there");
    case GolferMove::Action::NewHomeDiffers: return QStringLiteral("new home differs: **the new home wins**");
    case GolferMove::Action::Empty:          return QStringLiteral("empty: nothing to move");
    case GolferMove::Action::ShotGone:       return QStringLiteral("**the shot's folder is gone**: kept in diagnostics.json");
    }
    return {};
}

QString localTime(const QDateTime &t) { return t.isValid() ? t.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("-"); }

QString cell(QString s) { return s.replace(QLatin1Char('|'), QStringLiteral("\\|")).replace(QLatin1Char('\n'), QLatin1Char(' ')); }

} // namespace

QString reportMarkdown(const Result &r)
{
    QString md;
    QTextStream o(&md);
    const bool write = r.options.write;
    o << "# " << kToolName << " — " << (write ? "WRITE run" : "dry run (nothing was written)") << "\n\n";
    o << "| | |\n|---|---|\n";
    o << "| when | " << r.at.toLocalTime().toString(Qt::ISODate) << " (" << isoUtc(r.at) << ") |\n";
    o << "| machine | " << r.machine << " |\n";
    o << "| tool | " << kToolName << " " << kToolVersion << " |\n";
    o << "| library | `" << r.options.library << "` |\n";
    o << "| settings | " << (r.options.settingsFile.isEmpty() ? QStringLiteral("none given (M1 and M4 see no settings record)")
                                                               : QStringLiteral("`%1` (read from a private copy)").arg(r.options.settingsFile)) << " |\n";
    if (!r.libraryInSettings.isEmpty()) o << "| the settings' library | `" << r.libraryInSettings << "` |\n";
    o << "| other machine's export | " << (r.options.otherMachineExport.isEmpty() ? QStringLiteral("none (H-09: the other machine's records are not compared)")
                                                                                  : QStringLiteral("`%1` (%2)").arg(r.options.otherMachineExport, r.otherMachine)) << " |\n";
    o << "| decisions | " << (r.options.decisionsFile.isEmpty() ? QStringLiteral("none") : QStringLiteral("`%1`").arg(r.options.decisionsFile)) << " |\n";
    if (write) o << "| backups | `" << r.log.backupRoot << "` |\n";
    o << "\n";
    if (r.refused) { o << "## REFUSED\n\n" << r.refusal << "\n\n"; }

    o << "## Before a write\n\n";
    o << "- Folder locks held in the library: " << (r.locksHeld.isEmpty() ? QStringLiteral("none") : QString::number(r.locksHeld.size())) << "\n";
    for (const QString &l : r.locksHeld) o << "  - `" << l << "`\n";
    o << "- Sessions in progress (never touched; a write refuses while any is, D11): "
      << (r.inProgress.isEmpty() ? QStringLiteral("none") : QString::number(r.inProgress.size())) << "\n";
    for (const QString &s : r.inProgress) o << "  - `" << s << "`\n";
    if (!r.notAthleteFolders.isEmpty())
        o << "- Library folders that are not athlete folders (no session, no record; not migrated): "
          << r.notAthleteFolders.join(QStringLiteral(", ")) << "\n";
    o << "- Files a write would create, change or remove: **" << r.writesPlanned() << "**\n\n";

    // ── M1
    o << "## M1 — the athlete records (settings → athlete.json)\n\n";
    o << "| athlete folder | sessions | this machine | other machine | library athlete.json | id M1 settles | outcome |\n|---|---|---|---|---|---|---|\n";
    for (const AthletePlan &p : r.athletes) {
        const auto ids = [](const QList<RecordSource> &l) {
            QStringList s;
            for (const RecordSource &x : l) s << QStringLiteral("%1 (%2)").arg(recordName(x.record), x.id.toString());
            return s.isEmpty() ? QStringLiteral("-") : s.join(QStringLiteral("; "));
        };
        o << "| " << p.token << (p.folderExists ? "" : " (no folder)") << " | " << p.sessions << " | " << cell(ids(p.thisMachine)) << " | "
          << cell(ids(p.otherMachine)) << " | "
          << (p.library ? cell(QStringLiteral("%1 (%2)").arg(recordName(p.library->record), p.library->id.toString())) : QStringLiteral("-"))
          << " | " << (p.id.isNull() ? QStringLiteral("-") : p.id.toString() + (p.idProvisional ? QStringLiteral(" (provisional)") : QString()))
          << " | " << outcomeWord(p.outcome) << " |\n";
    }
    o << "\n";
    for (const AthletePlan &p : r.athletes) {
        if (p.why.isEmpty() && p.notes.isEmpty() && p.differences.isEmpty() && p.headerIds.isEmpty()) continue;
        o << "### " << p.token << "\n\n";
        if (!p.why.isEmpty()) o << "- " << p.why << "\n";
        for (const QString &n : p.notes) o << "- " << n << "\n";
        if (!p.headerIds.isEmpty()) {
            QStringList h;
            for (auto it = p.headerIds.constBegin(); it != p.headerIds.constEnd(); ++it) h << QStringLiteral("%1 ×%2").arg(it.key()).arg(it.value());
            QStringList n;
            for (auto it = p.headerNames.constBegin(); it != p.headerNames.constEnd(); ++it) n << QStringLiteral("%1 ×%2").arg(it.key()).arg(it.value());
            o << "- the swings record athlete.uuid " << h.join(QStringLiteral(", ")) << "; athlete.name " << n.join(QStringLiteral(", ")) << "\n";
        }
        if (!p.settingsGroupsToRemove.isEmpty() && p.outcome != AthletePlan::Outcome::Blocked)
            o << "- this machine's settings group(s) removed after the read-back: " << p.settingsGroupsToRemove.join(QStringLiteral(", ")) << "\n";
        if (!p.differences.isEmpty()) {
            o << "\n| field | this machine | other machine | library | written |\n|---|---|---|---|---|\n";
            for (const FieldDifference &d : p.differences)
                o << "| " << d.key << " | " << cell(shorten(compact(d.thisMachine))) << " | " << cell(shorten(compact(d.otherMachine))) << " | "
                  << cell(shorten(compact(d.library))) << " | " << (d.decided ? cell(shorten(compact(d.chosen))) : QStringLiteral("**pick one**")) << " |\n";
        }
        o << "\n";
    }
    if (!r.otherIdReferences.isEmpty()) {
        o << "This machine's settings that name an id M1 replaces (re-keyed by the write):\n\n";
        for (const QString &s : r.otherIdReferences) o << "- " << s << "\n";
        o << "\n";
    }

    // ── M2
    o << "## M2 — SessionId and session.json for every session\n\n";
    QString lastToken;
    int writes = 0, corrections = 0;
    for (const SessionPlan &s : r.sessions) {
        if (s.athleteToken != lastToken) {
            lastToken = s.athleteToken;
            o << "\n### " << s.athleteToken << "\n\n";
            o << "| session | swings | SessionId | start (local) | end (local) | type (shots / name) | outcome | note |\n|---|---|---|---|---|---|---|---|\n";
        }
        QStringList shots;
        for (auto it = s.shotTypes.constBegin(); it != s.shotTypes.constEnd(); ++it)
            shots << QStringLiteral("%1 ×%2").arg(it.key().isEmpty() ? QStringLiteral("(none)") : it.key()).arg(it.value());
        QStringList note;
        if (!s.why.isEmpty()) note << s.why;
        if (s.ownerFromHeaders) note << QStringLiteral("owner from the swings' athlete.uuid");
        if (s.ownerProvisional) note << QStringLiteral("provisional id (M1 not settled)");
        if (s.timesFromFolderDate) note << QStringLiteral("**no swing: the start is the folder's date**");
        if (s.typeDisagreement) note << QStringLiteral("**type disagreement**");
        if (s.outcome == SessionPlan::Outcome::CorrectStart) note << QStringLiteral("the app's start was %1").arg(localTime(s.previousStartUtc));
        if (s.startUtc.isValid() && s.nameDay.isValid() && s.startUtc.toLocalTime().date() != s.nameDay)
            note << QStringLiteral("first swing on %1, not the folder's date").arg(s.startUtc.toLocalTime().date().toString(Qt::ISODate));
        if (s.archived) note << QStringLiteral("archived: the mirror's copy too");
        if (!s.swingUuids.isEmpty() && !s.owner.isNull() && !s.swingUuids.contains(s.owner.toString()) && !s.ownerFromHeaders)
            note << QStringLiteral("its swings record %1").arg(s.swingUuids.join(QStringLiteral(", ")));
        o << "| " << s.name << " | " << s.swings << " | " << (s.id.isNull() ? QStringLiteral("-") : s.id.toString()) << " | "
          << localTime(s.startUtc) << " | " << localTime(s.endUtc) << " | " << cell((s.type.isEmpty() ? QStringLiteral("(none)") : s.type)
          + QStringLiteral(" (") + shots.join(QStringLiteral(", ")) + QStringLiteral(" / ") + (s.nameType.isEmpty() ? QStringLiteral("none") : s.nameType) + QStringLiteral(")"))
          << " | " << outcomeWord(s.outcome) << " | " << cell(note.join(QStringLiteral("; "))) << " |\n";
        if (s.outcome == SessionPlan::Outcome::Write) ++writes;
        if (s.outcome == SessionPlan::Outcome::CorrectStart) ++corrections;
    }
    o << "\n" << writes << " session file(s) to write, " << corrections << " app-backfilled start(s) to correct.\n\n";
    o << "### Type disagreements\n\n";
    int td = 0;
    for (const SessionPlan &s : r.sessions)
        if (s.typeDisagreement) {
            ++td;
            QStringList shots;
            for (auto it = s.shotTypes.constBegin(); it != s.shotTypes.constEnd(); ++it)
                shots << QStringLiteral("%1 ×%2").arg(it.key().isEmpty() ? QStringLiteral("(none)") : it.key()).arg(it.value());
            o << "- " << s.athleteToken << "/" << s.name << ": shots " << shots.join(QStringLiteral(", ")) << ", name "
              << (s.nameType.isEmpty() ? QStringLiteral("none") : s.nameType) << " → "
              << (s.typeNeedsDecision ? (s.type.isEmpty() ? QStringLiteral("**pick one**") : s.type + QStringLiteral(" (decided)"))
                                      : s.type + QStringLiteral(" (the shots')")) << "\n";
        }
    if (!td) o << "None.\n";
    o << "\n### Same-day sessions whose order changes (name order ≠ start order; parity run B's expected set)\n\n";
    if (r.orderChanges.isEmpty()) o << "None.\n";
    for (const DayOrderChange &c : r.orderChanges)
        o << "- " << c.athleteToken << ", " << c.day.toString(Qt::ISODate) << ": by name " << c.byName.join(QStringLiteral(" → "))
          << "; by start " << c.byStart.join(QStringLiteral(" → ")) << "\n";
    o << "\n";

    // ── M3
    o << "## M3 — the golfer's input out of diagnostics.json\n\n";
    int reviews = 0, inputs = 0, cleaned = 0;
    for (const GolferPlan &g : r.golfer) {
        if (g.skipped) continue;
        reviews += int(g.reviewEdits.size());
        inputs  += g.writeSessionInput ? 1 : 0;
        cleaned += g.keysToRemove.isEmpty() ? 0 : 1;
    }
    o << "- session_input.json written: " << inputs << "; swing files rewritten (a shot's review): **" << reviews
      << "**; diagnostics.json files with older keys removed: " << cleaned << "\n";
    o << "- tendencies.json dropped (rebuilt from the new homes, J-45): "
      << (r.tendenciesToDrop.isEmpty() ? QStringLiteral("none") : r.tendenciesToDrop.join(QStringLiteral(", "))) << "\n\n";
    for (const GolferPlan &g : r.golfer) {
        o << "### " << g.athleteToken << "/" << g.name << "\n\n";
        if (g.skipped) o << "- **skipped**: " << g.why << "\n";
        o << "- older keys in diagnostics.json: " << (g.legacyKeys.isEmpty() ? QStringLiteral("none") : g.legacyKeys.join(QStringLiteral(", "))) << "\n";
        for (const GolferMove &m : g.moves)
            o << "  - `" << m.key << "` → " << m.target << (m.value.isEmpty() ? QString() : QStringLiteral(" = ") + cell(shorten(m.value)))
              << ": " << moveWord(m.action) << "\n";
        if (g.writeSessionInput) o << "- session_input.json gains: " << cell(shorten(QString::fromUtf8(QJsonDocument(g.sessionInputFields).toJson(QJsonDocument::Compact)), 300)) << "\n";
        if (!g.reviewEdits.isEmpty()) {
            QStringList s;
            for (auto it = g.reviewEdits.constBegin(); it != g.reviewEdits.constEnd(); ++it) s << SwingPaths::swingFolderName(it.key());
            o << "- swing files rewritten: " << s.join(QStringLiteral(", ")) << "\n";
        }
        if (!g.keysToRemove.isEmpty()) o << "- removed from diagnostics.json after the check: " << g.keysToRemove.join(QStringLiteral(", ")) << "\n";
        if (!g.keysKept.isEmpty()) o << "- **kept** in diagnostics.json (an entry has no shot to go to; you decide): " << g.keysKept.join(QStringLiteral(", ")) << "\n";
        for (const RecoveredInput &x : g.recovered)
            o << "- **golfer input only a backup holds**: `" << x.backupFile << "` " << x.field << " = " << cell(x.value)
              << " (the session holds none) → " << (x.restore ? QStringLiteral("**restored** (decided)") : QStringLiteral("restore? (decisions: restore)")) << "\n";
        o << "\n";
    }
    int recovered = 0;
    for (const GolferPlan &g : r.golfer) recovered += int(g.recovered.size());
    o << "Sessions whose backups hold golfer input the live file lacks: " << recovered << " case(s).\n\n";

    // ── M4
    o << "## M4 — the Wrist chart's reference ★\n\n";
    o << "- ui/wristReferenceSwingDir: " << (r.wrist.settingValue.isEmpty() ? QStringLiteral("(not set)") : QStringLiteral("`%1`").arg(r.wrist.settingValue)) << "\n";
    switch (r.wrist.outcome) {
    case WristPlan::Outcome::NotSet:         o << "- nothing to convert (" << r.wrist.why << ")\n"; break;
    case WristPlan::Outcome::NotFound:       o << "- **not found**: " << r.wrist.why << "\n"; break;
    case WristPlan::Outcome::Write:          o << "- → **" << r.wrist.shot.toString() << "** on " << r.wrist.athleteToken << "'s record\n"; break;
    case WristPlan::Outcome::AlreadyThere:   o << "- → " << r.wrist.shot.toString() << ": already on the record\n"; break;
    case WristPlan::Outcome::RecordHasOther: o << "- → " << r.wrist.shot.toString() << ": " << r.wrist.why << "\n"; break;
    case WristPlan::Outcome::Waits:          o << "- → " << r.wrist.shot.toString() << " (provisional): " << r.wrist.why << "\n"; break;
    }
    o << "- the setting itself is left as it is (the Charts views still carry the path until step 4)\n\n";

    if (write) {
        o << "## What the write did\n\n";
        o << "- written: " << r.log.written.size() << "; removed: " << r.log.removed.size() << "; settings changes: " << r.log.settingsChanges.size()
          << "; failures: **" << r.log.failures.size() << "**\n";
        for (const QString &f : r.log.failures) o << "  - FAILED: " << f << "\n";
        for (const QString &f : r.log.written) o << "  - wrote `" << f << "`\n";
        for (const QString &f : r.log.removed) o << "  - removed `" << f << "`\n";
        for (const QString &f : r.log.settingsChanges) o << "  - settings: " << f << "\n";
        o << "\n";
    }
    o << "## Files read\n\n" << r.filesRead.size() << " file(s) (the list: --read-list).\n";
    for (const QString &w : r.warnings) o << "\n> " << w << "\n";
    return md;
}

} // namespace pinpoint::migrate
