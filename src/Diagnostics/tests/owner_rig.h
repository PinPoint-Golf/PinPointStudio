// owner_rig.h — THE OWNERS A VIEW TEST DRIVES (refactor 5.4a; build/refactor/step-5/DUTIES-5.4.md Q4, J-72).
//
// Until 5.4a the view tests made a SessionDiagnosticsModel of their own and fed it folders (activateSession, ingestShot):
// the old engine. Now a test reaches a session the way the app does: an Athlete over an athlete folder of a temporary
// library holds the Session; its SessionDiagnostics (synchronous doors, the real firing reader, persisting into the
// temporary library) judges every shot; the screens' door (SessionDiagnosticsAdapters) hands out the Session's thin
// adapter, which runs the view half only. The golfer's commands reach the Session and its Shots through `app`
// (SessionDiagnosticsModel::setShotCommands), as main.cpp wires them.
//
// One ViewRig per athlete folder. A legacy folder (no session.json) is held as an ended session, so its adapter shows
// it at Closing. A live session is started through the Athlete and each shot added through the Session (EVT-01).
//
// SEEDING ROWS (Q4): a test that writes the rows itself gives the folder its documents first (seed() stages one where
// none is), and seed() stamps each row current for the owner — its document as it stands and the owner's findings stamp
// for that session — and drops the derived data, so the owner keeps the rows as written and derives again from them.
// The session is released before the file is written, so no release writes over it.
#pragma once

#include "../../App/app.h"
#include "../../Core/work_pool.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/rule_data.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"
#include "../derived/diagnostic_ledger.h"
#include "../../Export/athlete_store.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/session_diagnostics_adapter.h"
#include "../../Gui/diagnostics/session_diagnostics_model.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimeZone>
#include <QUuid>

#include <functional>
#include <map>
#include <memory>

namespace owner_rig {

inline bool pump(const std::function<bool()> &done, int ms = 30000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return true;
}

struct ViewRig {
    QString                                                  athleteDir;
    pinpoint::SessionStore                                   store;
    std::unique_ptr<pinpoint::SwingStoreRecordingRepository> recordings;
    std::shared_ptr<pinpoint::RecordingReader>               reader;
    std::unique_ptr<pinpoint::WorkReceiver>                  receiver;
    std::unique_ptr<SessionDiagnosticsAdapters>              adapters;
    // THE ATHLETE AS THE APP MAKES IT (refactor 5.4a, round 2): through its store, so it has its repository and appends its
    // own tuning log (FILE-ATH-04: its one writer).
    std::unique_ptr<pinpoint::AthleteStore>                  athletes;
    std::unique_ptr<pinpoint::Athlete>                       owned;
    pinpoint::Athlete                                       *athlete = nullptr;
    std::unique_ptr<pinpoint::App>                           app;

    // `athleteId`: the athlete the folder's sessions name (a parity copy's athlete.json, refactor 5.5a's
    // charts_series_dump); empty, a new one (the suites' own folders).
    explicit ViewRig(const QString &dir, const QString &athleteId = QString()) : athleteDir(QDir::cleanPath(dir))
    {
        using namespace pinpoint;
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);   // never the OS trash
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        adapters = std::make_unique<SessionDiagnosticsAdapters>();
        QDir().mkpath(athleteDir);
        const QFileInfo fi(athleteDir);
        const QString lib = fi.absolutePath();
        // Its repository is the app's (AthleteStore), so it appends its own tuning log; made here rather than by
        // AthleteStore::create, which numbers a folder that is already there (the suites make theirs first).
        athletes = std::make_unique<AthleteStore>([lib] { return lib; }, &store);
        athletes->setOsTrashForTest(false);
        athletes->setLockWaitForTest(200);
        const AthleteId id = AthleteId::fromString(athleteId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : athleteId);
        owned = std::make_unique<Athlete>(athletes.get(), &store, id);
        athlete = owned.get();
        AthleteRecord r;
        r.name = fi.fileName();   // the token the store composes the folder from: the folder's own name
        athlete->setStored(r, athleteDir, SessionOwner{ id, r.name, lib }, Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        DiagnosticsDoors d;
        d.receiver        = receiver.get();
        d.ruleData        = RuleData::installed();
        d.reader          = std::make_shared<SwingStoreFiringReader>(store, *recordings);   // parses a miss, writes the sidecar
        d.persist         = true;    // the temporary library only
        d.synchronous     = true;    // every ask comes back inline
        d.writeDebounceMs = 0;
        d.attach          = &SessionDiagnostics::attach;
        athlete->setDiagnosticsDoors(d);
        app = std::make_unique<App>([this] { return athlete; });
    }
    ~ViewRig()
    {
        SessionDiagnosticsModel::setShotCommands(nullptr);
        if (athlete)
            for (pinpoint::Session *s : athlete->loadedSessions()) dropViews(s);
        app.reset();
        athlete = nullptr;
        owned.reset();
        athletes.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        adapters.reset();
    }

    static void dropViews(pinpoint::Session *s)
    {
        if (!s) return;
        for (SessionDiagnosticsModel *m : s->findChildren<SessionDiagnosticsModel *>(Qt::FindDirectChildrenOnly))
            while (m->views() > 0) m->dropView();
    }
    static void settle(pinpoint::SessionDiagnostics *d)
    {
        if (d) pump([d] { return d->idle() && !d->writePending(); }, 20000);
    }
    pinpoint::Session *loadedAt(const QString &dir) const
    {
        for (pinpoint::Session *s : athlete->loadedSessions())
            if (s->entry() && QDir::cleanPath(s->entry()->folder) == QDir::cleanPath(dir)) return s;
        return nullptr;
    }
    // What it kept written, its views let go, and the Session released (deleted on the next turn).
    bool release(const QString &dir)
    {
        pinpoint::Session *s = loadedAt(dir);
        if (!s) return true;
        if (s->inProgress()) s->end();
        dropViews(s);
        if (s->diagnostics()) {
            settle(s->diagnostics());
            s->diagnostics()->flush(/*final=*/true);
        }
        const bool ok = athlete->releaseSession(s->id());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return ok;
    }

    // THE PANEL'S ADAPTER OF A SESSION, as a stage panel binds it: a view bound, the cadence, synchronous (the history
    // read inline), pointed at its folder. The owner has judged every shot and written its file first.
    SessionDiagnosticsModel *viewOf(pinpoint::Session *s, const QString &cadence = QStringLiteral("everyShot"),
                                    const QString &surface = QStringLiteral("panel"))
    {
        if (!s) return nullptr;
        SessionDiagnosticsModel::setShotCommands(app.get());
        pinpoint::SessionDiagnostics *d = s->diagnostics();
        settle(d);
        if (d) d->flush(/*final=*/true);
        SessionDiagnosticsModel *m = adapters->of(s, surface);
        if (!m) return nullptr;
        if (m->views() == 0) m->addView();
        m->setSynchronous(true);
        m->setCadence(cadence);
        m->activateSession(s->entry() ? s->entry()->folder : QString());
        return m;
    }
    // A FOLDER OPENED AFRESH (the old tests' fresh model and activateSession): any Session of it released, then held
    // again — every document changed since judged again, every new swing judged — and its adapter.
    SessionDiagnosticsModel *open(const QString &dir, const QString &cadence = QStringLiteral("everyShot"))
    {
        release(dir);
        return viewOf(athlete->sessionAt(dir), cadence);
    }

    // A live session, and a shot of it: the fixture's document written into the swing folder the Session allocated, its
    // mtime pinned (`mtimeMs`, when given), then EVT-01.
    pinpoint::Session *startLive() { return athlete->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type")); }
    pinpoint::Shot *addLive(pinpoint::Session *s, const QString &fixtureDoc, qint64 mtimeMs = 0)
    {
        pinpoint::Shot *shot = s ? s->addShot() : nullptr;
        if (!shot) return nullptr;
        const QString doc = QDir(shot->folder()).filePath(QStringLiteral("swing.json"));
        if (!QFile::copy(fixtureDoc, doc)) return nullptr;
        QFile f(doc);
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (mtimeMs > 0 && f.open(QIODevice::ReadWrite)) {
            f.setFileTime(QDateTime::fromMSecsSinceEpoch(mtimeMs, QTimeZone::UTC), QFileDevice::FileModificationTime);
            f.close();
        }
        s->shotDocumentWritten(shot->ref());   // EVT-01
        settle(s->diagnostics());
        return shot;
    }

    // A SESSION FILE FOR A LEGACY FOLDER (FILE-SESS-01, as migration M2 left every session of the library: 0 without one on
    // 22 Nov): its athlete this rig's, ended, started on the day its name gives (else now). Written before the folder is
    // first held, so the golfer's commands reach the Session and its Shots by the id the file gives (the adapter names a
    // shot by its folder's session file until step 5.4b). True when a file is there now.
    bool ensureSessionFile(const QString &dir)
    {
        const QString path = pinpoint::SessionFiles::sessionFile(dir);
        if (QFileInfo::exists(path)) return true;
        const QDate day = QDate::fromString(QFileInfo(dir).fileName().left(10), Qt::ISODate);
        const QDateTime started = day.isValid() ? QDateTime(day, QTime(12, 0), QTimeZone::UTC) : QDateTime::currentDateTimeUtc();
        const QJsonObject o{
            { QStringLiteral("schema"), QStringLiteral("pinpoint.session/1") },
            { QStringLiteral("sessionId"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
            { QStringLiteral("athleteId"), athlete->id().toString() },
            { QStringLiteral("type"), QStringLiteral("swing") },
            { QStringLiteral("status"), QStringLiteral("ended") },
            { QStringLiteral("startedAt"), started.toString(Qt::ISODateWithMs) },
        };
        QDir().mkpath(dir);
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(QJsonDocument(o).toJson()) > 0;
    }

    // THE OWNER'S FINDINGS STAMP FOR A FOLDER AS IT STANDS (its club baseline and documents included): held once, read,
    // released (the load writes what it judged; seed() writes over it).
    QString ownerStampOf(const QString &dir)
    {
        release(dir);
        pinpoint::Session *s = athlete->sessionAt(dir);
        pinpoint::SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        settle(d);
        const QString stamp = d ? d->findingsStamp() : QString();
        release(dir);
        return stamp;
    }
    // Rows written by the test, kept by the owner as written (the header). `fixtureDoc`: the document staged for a row
    // whose swing folder has none.
    bool seed(const QString &dir, const QJsonObject &rootIn, const QString &fixtureDoc)
    {
        QJsonObject root = rootIn;
        const std::vector<pinpoint::analysis::ShotRecord> rows =
            pinpoint::analysis::fromJson(root.value(QStringLiteral("ledger")).toObject());
        for (const pinpoint::analysis::ShotRecord &r : rows) {
            const QString sw = QDir(dir).filePath(QStringLiteral("swing_%1").arg(r.shotId, 4, 10, QLatin1Char('0')));
            if (pinpoint::SwingStore::info(sw).exists()) continue;
            QDir().mkpath(sw);
            if (!QFile::copy(fixtureDoc, QDir(sw).filePath(QStringLiteral("swing.json")))) return false;
            QFile(QDir(sw).filePath(QStringLiteral("swing.json"))).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
        const QString stamp = ownerStampOf(dir);
        if (stamp.isEmpty()) return false;
        QJsonObject session = root.value(QStringLiteral("session")).toObject();
        session.remove(QStringLiteral("derived"));
        session.remove(QStringLiteral("unread"));
        QJsonObject graded;
        for (const pinpoint::analysis::ShotRecord &r : rows) {
            const QString sw = QDir(dir).filePath(QStringLiteral("swing_%1").arg(r.shotId, 4, 10, QLatin1Char('0')));
            const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(sw);
            graded.insert(QString::number(r.shotId), QJsonObject{ { QStringLiteral("size"), double(doc.size) },
                                                                  { QStringLiteral("mtimeMs"), double(doc.mtimeMs) },
                                                                  { QStringLiteral("content"), stamp } });
        }
        session[QStringLiteral("gradedFrom")] = graded;
        root[QStringLiteral("session")] = session;
        if (!root.contains(QStringLiteral("schemaVersion"))) root[QStringLiteral("schemaVersion")] = 1;
        QFile f(pinpoint::SessionFiles::diagnosticsFile(dir));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        return true;
    }
};

// One rig per athlete folder, for the whole run (rigs() is cleared before main returns).
inline std::map<QString, std::unique_ptr<ViewRig>> &rigs()
{
    static std::map<QString, std::unique_ptr<ViewRig>> r;
    return r;
}
inline ViewRig &rigFor(const QString &athleteDir)
{
    const QString key = QDir::cleanPath(athleteDir);
    auto &slot = rigs()[key];
    if (!slot) slot = std::make_unique<ViewRig>(key);
    return *slot;
}
inline ViewRig &rigOfSession(const QString &sessionDir)
{
    return rigFor(QFileInfo(QDir::cleanPath(sessionDir)).absolutePath());
}

} // namespace owner_rig
