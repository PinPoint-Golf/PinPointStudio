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

// post_shot_bench — THE POST-SHOT GUI PATH, HEADLESS, FOR A WHOLE SESSION (refactor 3.7-S, the post-shot stall;
// build/refactor/step-3/3.7/NOTES-S.md).
//
// After every live shot the cabin's GUI thread blocked for a time that climbed with each shot of a session (0.7 s →
// 3.3 s) and reset at a new one. This drives the same GUI-thread path with the app's real classes, no QML and no
// cameras, for N shots of ONE session on a scratch library, and prints the [PostShot] line (post_shot_trace.h) of
// each shot plus a per-step table: does each step's cost grow with the shot count?
//
// The composition is main.cpp's: SessionStore, the swing door and reader, an Athlete with its shot doors, `app` as
// the shot-command door, the carousel's ShotListModel, the diagnostics panel's SessionDiagnosticsModel (async, as QML
// makes it) and, with --path old --parallel 1, the parallel run's in-memory doors (its comparison hook was deleted in
// refactor 3.10: the owner runs beside the old panel, uncompared). Each shot, in
// the cabin's order (D22-STALL-NOTES §1):
//
//   join      ShotProcessor::onSwingDocWritten's work: EVT-01 (Session::shotDocumentWritten), the replay detail
//             converted to JavaScript once (notify.js: what the first QML read pays; the bindings themselves are not
//             here), the carousel row (addShot), then shotProcessed's synchronous slots — the launch monitor's ball
//             reading into this shot (lm.ball: the Shot's command, its lock and document, the row refresh) and the
//             panel's ingestShot
//   lm.club   the PREVIOUS shot's club data (Foresight Premier sends it with the next shot): the Shot's command, the
//             controller's refresh, and the carousel's _swingRewritten (a second refresh; on --path old the panel's
//             regradeShot, which since 3.7-c the carousel no longer asks of an adapter)
//   (events)  pumped until the panel model and the new owner are idle, so each settle (rebuild, persist) and
//             each kept result lands inside the shot's window, as live
//
// Not driven: QML (bindings, the ledger and carousel delegates), the Charts/Consistency SessionSeriesModel, the media
// export and analysis (workers), the GPU. The documents are real cabin documents (copies; --fixtures), rotated.
//
//   cmake --build build/tests --target post_shot_bench -j 6
//   post_shot_bench --fixtures <dir of swing.ppsw copies> --library <empty scratch dir> [--shots 40] [--parallel 1]
//                   [--path new|old] [--view 1|0] [--cards 12] [--csv out.csv]
//
// --path new (the default; refactor 3.7-a): the app since the stage panel's cut-over — the app's doors
// (appDiagnosticsDoors: the owner always attached, persisting, on the one pool) and the panel's THIN ADAPTER
// (SessionDiagnosticsAdapters, a view bound), so a shot's window holds the owner's keep and diagnosis (`owner`), its
// debounced write (`owner.write`, 1.5 s later, as the app) and the adapter's view work (`adapter`, with the panel's QML
// read under it). --path old: HEAD before 3.7-a (the panel's own engine model; --parallel 1 adds the in-memory owner).
//
// --cards N (12; 0 none; refactor 3.7-c): the carousel cards whose pip rows read the panel's model on each surfaceChanged
// (PpShotCard: the swing's id by its folder, its pips, its fired count), timed as `qml.pips` inside the panel's emit.
//
// NOT a ctest: it times, it does not check, and it writes a library (refused unless --library is empty or new).

#include "../../src/App/app.h"
#include "../../src/Core/post_shot_trace.h"
#include "../../src/Core/work_pool.h"
#include "../../src/DomainModel/athlete.h"
#include "../../src/DomainModel/session.h"
#include "../../src/DomainModel/session_diagnostics.h"
#include "../../src/DomainModel/shot.h"
#include "../../src/Export/firing_reader_store.h"
#include "../../src/Export/recording_loader.h"
#include "../../src/Export/rule_data_install.h"
#include "../../src/Export/session_store.h"
#include "../../src/Export/swing_doc.h"
#include "../../src/Export/swing_store.h"
#include "../../src/Export/swing_store_repository.h"
#include "../../src/Gui/diagnostics/diag_parallel_run.h"
#include "../../src/Gui/diagnostics/session_diagnostics_adapter.h"
#include "../../src/Gui/diagnostics/session_diagnostics_model.h"
#include "../../src/Gui/shot/qml_payload.h"
#include "../../src/Gui/shot/shot_list_model.h"
#include "../../src/LaunchMonitor/launch_monitor_reading.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaProperty>
#include <QJsonObject>
#include <QMap>
#include <QStandardPaths>
#include <QThread>

#include <cstdio>
#include <functional>
#include <memory>

#ifndef PP_DIAGRES
#  define PP_DIAGRES "."
#endif

using namespace pinpoint;

namespace {

const QString   kToken   = QStringLiteral("Bench-Golfer");
const AthleteId kAthlete = AthleteId::fromString(u"5b1f0e3a-8c2d-4e7f-9a61-3d4c2b1a0f98");

bool pump(const std::function<bool()> &done, int ms = 60000)
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

// The fixture's own reading, as a connector hands it over (every field of its launchMonitor block).
lm::LaunchMonitorReading readingOf(const QJsonObject &block, int swing)
{
    lm::LaunchMonitorReading r;
    r.deviceShotId = QStringLiteral("bench-%1").arg(swing);
    r.deviceClub   = block.value(QStringLiteral("deviceClub")).toString();
    r.deviceKind   = block.value(QStringLiteral("kind")).toString();
    r.sourcePath   = block.value(QStringLiteral("sourcePath")).toString();
    r.readAtMs     = QDateTime::currentMSecsSinceEpoch();
    for (const lm::FieldDef &f : lm::fieldDefs()) {
        const QJsonValue v = block.value(QLatin1String(f.rawName));
        if (v.isDouble()) r.*(f.member) = v.toDouble();
    }
    return r;
}

struct Fixture {
    QString     ppsw;
    QJsonObject lmBlock;
};

// Each step's ms in one shot's window, by its path ("join/announce/lm.ball/lm.write/doc"), repeats summed.
using StepTable = QMap<QString, QPair<double, int>>;
StepTable tableOf(const PostShotTrace::Window &w)
{
    StepTable t;
    QStringList path;
    for (const PostShotTrace::Step &s : w.steps) {
        while (path.size() > s.depth) path.removeLast();
        path << QLatin1String(s.name);
        auto &cell = t[path.join(QLatin1Char('/'))];
        cell.first += s.ns / 1.0e6;
        cell.second += 1;
    }
    return t;
}

// ONE QML READER, AT LEAST: what a binding pays to read a value — the C++ getter (some compute on read) and the value
// materialised as JavaScript (a QVariantList/QVariantMap property becomes a fresh JS tree on every read). The real panel
// and carousel have several bindings per property; this is the floor, nested where QML pays it (inside the emit).
void readLikeQml(QJSEngine &engine, const QVariant &v)
{
    const QJsonValue j = QJsonValue::fromVariant(v);
    (void)engine.toScriptValue(j);
}
void readAllProperties(QJSEngine &engine, const QObject &o)
{
    const QMetaObject *mo = o.metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty prop = mo->property(i);
        PostShotTrace::Scope t(prop.name());   // moc's string: static
        readLikeQml(engine, prop.read(&o));
    }
}
// The carousel's delegate (PpShotCard) reads ONLY its required properties: these roles, never analysisDetail.
const QList<QByteArray> &cardRoles()
{
    static const QList<QByteArray> r{ "shotId", "ordinal", "timestampLabel", "club", "hasVideo", "thumbnailSource", "tracePoints",
                                      "score", "rating", "note", "metrics", "swingDir", "dataWarning", "dataWarningDetail" };
    return r;
}
void readRow(QJSEngine &engine, const QAbstractItemModel &m, int row, const QList<int> &changed)
{
    const QModelIndex ix = m.index(row, 0);
    const QHash<int, QByteArray> names = m.roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        const int at = int(cardRoles().indexOf(it.value()));
        if (at < 0 || (!changed.isEmpty() && !changed.contains(it.key()))) continue;
        PostShotTrace::Scope t(cardRoles().at(at).constData());   // a static list: the name outlives the line
        readLikeQml(engine, m.data(ix, it.key()));
    }
}

double slope(const QList<double> &y)   // least squares, ms per shot
{
    const int n = y.size();
    if (n < 2) return 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; ++i) { sx += i; sy += y[i]; sxx += double(i) * i; sxy += i * y[i]; }
    const double d = n * sxx - sx * sx;
    return d == 0 ? 0 : (n * sxy - sx * sy) / d;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);   // nothing of the user's app data is read or written

    QCommandLineParser p;
    p.addOption({ QStringLiteral("fixtures"), QStringLiteral("folder of swing.ppsw copies (any depth)"), QStringLiteral("dir") });
    p.addOption({ QStringLiteral("library"), QStringLiteral("an empty or new scratch library folder"), QStringLiteral("dir") });
    p.addOption({ QStringLiteral("shots"), QStringLiteral("shots in the one session (40)"), QStringLiteral("n"), QStringLiteral("40") });
    p.addOption({ QStringLiteral("parallel"), QStringLiteral("old path: 1 the new owner in memory beside the old panel (1)"), QStringLiteral("0|1"), QStringLiteral("1") });
    p.addOption({ QStringLiteral("csv"), QStringLiteral("per-shot step table"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("path"), QStringLiteral("new: the app since 3.7-a (owner + adapter); old: the panel's own model (new)"),
                  QStringLiteral("new|old"), QStringLiteral("new") });
    p.addOption({ QStringLiteral("view"), QStringLiteral("new path: 1 a panel bound to the adapter, 0 the panel off (1)"),
                  QStringLiteral("0|1"), QStringLiteral("1") });
    p.addOption({ QStringLiteral("cards"), QStringLiteral("the carousel cards whose pip rows read the panel on each surfaceChanged (12; 0: none)"),
                  QStringLiteral("n"), QStringLiteral("12") });
    p.addOption({ QStringLiteral("quiet"), QStringLiteral("no [PostShot] line per shot") });
    p.addOption({ QStringLiteral("gc"), QStringLiteral("collect the JS engine's garbage after each shot, outside its window") });
    p.addHelpOption();
    p.process(app);

    for (const auto &[var, file] : { std::pair{ "PINPOINT_CORE_PACK", "core.json" }, std::pair{ "PINPOINT_CORE_NORMS", "norms.json" },
                                     std::pair{ "PINPOINT_CORE_FLIGHT_NORMS", "flight_norms.json" },
                                     std::pair{ "PINPOINT_CORE_CONTEXTS", "contexts.json" }, std::pair{ "PINPOINT_CORE_SCREENS", "screens.json" },
                                     std::pair{ "PINPOINT_CORE_DRILLS", "drills.json" }, std::pair{ "PINPOINT_CORE_GATE_WORDS", "gate_words.json" },
                                     std::pair{ "PINPOINT_CORE_REFERENCES", "references.json" } })
        if (qEnvironmentVariableIsEmpty(var)) qputenv(var, (QStringLiteral(PP_DIAGRES "/") + QLatin1String(file)).toLocal8Bit());

    const int  kShots   = std::max(2, p.value(QStringLiteral("shots")).toInt());
    const bool newPath  = p.value(QStringLiteral("path")) != QLatin1String("old");
    const bool parallel = !newPath && p.value(QStringLiteral("parallel")) == QLatin1String("1");
    const bool owners   = newPath || parallel;   // the pool, the receiver and an owner on the session
    const QString library = QDir::cleanPath(p.value(QStringLiteral("library")));
    if (library.isEmpty() || (QDir(library).exists() && !QDir(library).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))) {
        std::fprintf(stderr, "post_shot_bench: --library must name an empty or new folder\n");
        return 2;
    }
    QList<Fixture> fixtures;
    {
        QStringList found;
        QDirIterator it(p.value(QStringLiteral("fixtures")), { QStringLiteral("swing.ppsw") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) found << it.next();
        found.sort();
        for (const QString &f : std::as_const(found)) {
            const QJsonObject root = SwingStore::loadParts(QFileInfo(f).absolutePath(), {});
            fixtures.push_back({ f, root.value(QStringLiteral("launchMonitor")).toObject() });
        }
    }
    if (fixtures.isEmpty()) {
        std::fprintf(stderr, "post_shot_bench: no swing.ppsw under --fixtures\n");
        return 2;
    }

    RuleData &ruleData = installProcessRuleData();
    if (!ruleData.snapshot() || !ruleData.snapshot()->pack) {
        std::fprintf(stderr, "post_shot_bench: the shipped pack was not read (PINPOINT_CORE_PACK=%s)\n", qgetenv("PINPOINT_CORE_PACK").constData());
        return 1;
    }
    QJSEngine engine;   // the app's one engine (QmlPayload::setEngine): notify.js converts into it
    QmlPayload::setEngine(&engine);

    // ── main.cpp's composition ──
    QDir().mkpath(library + QLatin1Char('/') + kToken);
    SessionStore store;
    SwingStoreRecordingRepository recordings(store);
    SwingStoreRecordingReader     reader(store);
    std::unique_ptr<Athlete> athlete;
    std::function<WorkTarget(const WorkAddress &)> lookup;
    std::unique_ptr<WorkReceiver> receiver;
    std::unique_ptr<WorkPool>     pool;
    if (owners) {
        receiver = std::make_unique<WorkReceiver>([&lookup](const WorkAddress &a) { return lookup ? lookup(a) : WorkTarget{}; });
        pool     = std::make_unique<WorkPool>();
    }
    athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
    AthleteRecord rec;
    rec.name = QStringLiteral("Bench Golfer");
    athlete->setStored(rec, library + QLatin1Char('/') + kToken, SessionOwner{ kAthlete, rec.name, library }, Athlete::Source::Library, false);
    athlete->setShotDoors({ &recordings, std::shared_ptr<RecordingReader>(&reader, [](RecordingReader *) {}) });
    if (owners) {
        lookup = [&athlete](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        };
        // The app's doors (3.7-a: main.cpp's appDiagnosticsDoors), or the parallel run's in-memory ones (3.7-0).
        athlete->setDiagnosticsDoors(newPath ? appDiagnosticsDoors(pool.get(), receiver.get(), &ruleData, store, recordings)
                                             : parallel::doors(pool.get(), receiver.get(), &ruleData, store, recordings));
    }
    App appObject([&athlete] { return athlete.get(); });
    SessionDiagnosticsModel::setShotCommands(&appObject);
    ShotListModel shotModel;
    shotModel.setShotCommands(&appObject);

    Session *session = athlete->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
    if (!session || !session->entry()) {
        std::fprintf(stderr, "post_shot_bench: no session started\n");
        return 1;
    }
    const QString folder = session->entry()->folder;
    // The panel's model: 3.7-a's thin adapter (made by C++, a view bound), or HEAD's own model (made by QML: async).
    SessionDiagnosticsAdapters adapters;
    std::unique_ptr<SessionDiagnosticsModel> oldPanel;
    SessionDiagnosticsModel *panel = nullptr;
    if (newPath) {
        panel = adapters.of(session, QStringLiteral("panel"), folder);
        if (p.value(QStringLiteral("view")) != QLatin1String("0"))
            panel->addView();   // --view 0: the panel off — the adapter exists (as after a panel was once up) and idles
        panel->setBaselineAuthorityName(QStringLiteral("live"));
    } else {
        oldPanel = std::make_unique<SessionDiagnosticsModel>();
        panel = oldPanel.get();
    }
    panel->activateSession(folder);
    panel->waitForIdle(60000);
    // The QML readers' floor (readLikeQml): the panel's every property on each surfaceChanged, the carousel's new row
    // on rowsInserted and a refreshed row's roles on dataChanged — synchronously, inside the emit, as QML reads.
    QObject::connect(panel, &SessionDiagnosticsModel::surfaceChanged, panel, [&engine, panel] {
        PostShotTrace::Scope t("qml.panel");
        readAllProperties(engine, *panel);
    });
    QObject::connect(&shotModel, &QAbstractItemModel::rowsInserted, &shotModel, [&engine, &shotModel](const QModelIndex &, int first, int last) {
        PostShotTrace::Scope t("qml.row");
        for (int r = first; r <= last; ++r) readRow(engine, shotModel, r, {});
    });
    QObject::connect(&shotModel, &QAbstractItemModel::dataChanged, &shotModel,
                     [&engine, &shotModel](const QModelIndex &a, const QModelIndex &b, const QList<int> &roles) {
        PostShotTrace::Scope t("qml.row");
        for (int r = a.row(); r <= b.row(); ++r) readRow(engine, shotModel, r, roles);
    });
    SessionDiagnostics *owner = session->diagnostics();
    std::printf("post_shot_bench: %d shots, %lld fixture(s), path %s, parallel run %s, library %s\n", kShots,
                (long long)fixtures.size(), newPath ? "new (3.7-a: owner + adapter)" : "old (the panel's own model)",
                parallel ? "ON" : "off", qPrintable(library));
    std::printf("  session %s\n", qPrintable(folder));

    QList<StepTable> perShot;
    QList<qint64>    diagBytes;
    PostShotTrace::Window last;
    const bool quiet = p.isSet(QStringLiteral("quiet"));
    const auto sink = [&last, quiet](const QString &line, const PostShotTrace::Window &w) {
        last = w;
        if (!quiet) std::printf("%s\n", qPrintable(line));
    };

    QList<Shot *> shots;
    // THE CARDS' PIP ROWS (refactor 3.7-c): each card the strip has built (the newest `--cards`, as the strip scrolls to
    // the newest live shot) reads its pip row on every surfaceChanged of the model it reads: the swing's id by its folder,
    // its pips and its fired count (PpShotCard's pipRow) — synchronously, inside that model's emit, as QML reads them.
    // Panel up: the panel's model (adapter or old engine). Panel off, --path new (review B1: the pips are always there):
    // the session's CARDS adapter, which takes only what the pips read (`owner/pips`), with the carousel's view bound.
    const int kCards = std::max(0, p.value(QStringLiteral("cards")).toInt());
    SessionDiagnosticsModel *pipsModel = panel;
    if (newPath && p.value(QStringLiteral("view")) == QLatin1String("0") && kCards > 0) {
        pipsModel = adapters.of(session, QStringLiteral("cards"), folder);
        pipsModel->addView();   // PpShotCarousel._bindCardsView
    }
    if (kCards > 0)
        QObject::connect(pipsModel, &SessionDiagnosticsModel::surfaceChanged, pipsModel, [&engine, pipsModel, &shots, kCards] {
            PostShotTrace::Scope t("qml.pips");
            for (qsizetype k = std::max<qsizetype>(0, shots.size() - kCards); k < shots.size(); ++k) {
                const int id = pipsModel->shotIdForSwingDir(shots.at(k)->folder());
                readLikeQml(engine, id >= 0 ? QVariant(pipsModel->pipsFor(id)) : QVariant(QVariantList{}));
                readLikeQml(engine, QVariant(id >= 0 ? pipsModel->firedCountFor(id) : 0));
            }
        });
    // J-46 (3.7-c review B2): the row a late reading went into, as the carousel shows it — compared, outside the shot's
    // window, with the same row after one more refresh: equal means the controller's one refresh already shows it.
    const auto rowOf = [&shotModel](const QString &dir) {
        QVariantMap out;
        const QHash<int, QByteArray> names = shotModel.roleNames();
        for (int r = 0; r < shotModel.rowCount(); ++r) {
            const QModelIndex ix = shotModel.index(r, 0);
            if (shotModel.data(ix, names.key("swingDir")).toString() != dir) continue;
            for (const QByteArray &role : cardRoles()) out.insert(QString::fromLatin1(role), shotModel.data(ix, names.key(role)));
        }
        return out;
    };
    int lateRowsChecked = 0, lateRowsSame = 0, lateRowsWithReading = 0;
    for (int i = 1; i <= kShots; ++i) {
        // ── the capture worker's part (not the GUI thread's): the swing allocated, its document written ──
        Shot *shot = session->addShot();
        if (!shot) { std::fprintf(stderr, "post_shot_bench: shot %d not allocated\n", i); return 1; }
        const Fixture &fx = fixtures.at((i - 1) % fixtures.size());
        const QString dir = shot->folder();
        QFile::copy(fx.ppsw, dir + QStringLiteral("/swing.ppsw"));
        QFile(dir + QStringLiteral("/swing.ppsw")).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const PersistedShot built = SwingDocReader::readSwingJson(dir);   // stands for the worker's liveAnalysisDetail
        shots << shot;

        // ── the GUI thread's part ──
        PostShotTrace::begin(QStringLiteral("[PostShot] ") + shot->ref().toString(), sink, 60000);
        {
            PostShotTrace::Scope join("join");
            {
                PostShotTrace::Scope t("evt01");
                session->shotDocumentWritten(shot->ref());
            }
            QmlPayload detail;
            detail.set(built.analysisDetail);
            {
                PostShotTrace::Scope t("notify.js");
                (void)detail.js();
            }
            {
                PostShotTrace::Scope t("addShot");
                shotModel.addShot(dir, built.timestampLabel, built.club, true, QUrl(), QVariantList{}, built.score,
                                  built.metrics, detail.map(), built.dataWarning, built.dataWarningDetail);
            }
            PostShotTrace::Scope announce("announce");
            {   // LaunchMonitorController::onShotProcessed → applyToSwing: the ball reading into this shot
                PostShotTrace::Scope ball("lm.ball");
                bool replaced = false;
                {
                    PostShotTrace::Scope w("lm.write");
                    const ForwardResult f = appObject.completeCapture(sessionFolderOf(dir), shot->ref(), readingOf(fx.lmBlock, i), {}, &replaced);
                    if (!f.reached || !f.result.ok()) std::printf("  shot %d: the ball reading did not save: %s\n", i, qPrintable(f.result.error));
                }
                shotModel.refreshShot(dir);
            }
            panel->ingestShot(i, dir);   // PpSessionDiagnosticsPanel: onShotProcessed → ingestShot
        }
        if (i >= 2) {   // LaunchMonitorController::flushUpdates: the previous shot's club data
            Shot *prev = shots.at(i - 2);
            const QString pdir = prev->folder();
            const Fixture &pfx = fixtures.at((i - 2) % fixtures.size());
            PostShotTrace::Scope club("lm.club");
            {
                PostShotTrace::Scope w("lm.write");
                bool replaced = false;
                const ForwardResult f = appObject.completeCapture(sessionFolderOf(pdir), prev->ref(), readingOf(pfx.lmBlock, i - 1), {}, &replaced);
                if (!f.reached || !f.result.ok()) std::printf("  shot %d: the club data did not save: %s\n", i - 1, qPrintable(f.result.error));
            }
            shotModel.refreshShot(pdir);   // the controller's own refresh of the live row
            PostShotTrace::Scope rewritten("lm.rewritten");   // PpShotCarousel._swingRewritten
            // Since 3.7-c (J-46) the carousel neither refreshes the live row again nor asks an adapter to regrade (the
            // owner hears the late reading itself, EVT-22); --path old keeps HEAD's second refresh and regrade.
            if (!newPath) {
                shotModel.refreshShot(pdir);
                panel->regradeShot(pdir);
            }
        }
        // ── the queued deliveries: the panel's settles, the new owner's results ──
        pump([&] { return !panel->busy() && (!owner || (owner->idle() && !owner->writePending())); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        PostShotTrace::end();
        perShot << tableOf(last);
        if (i >= 2) {   // J-46's check, outside the window
            const QString pdir = shots.at(i - 2)->folder();
            const QVariantMap shown = rowOf(pdir);
            shotModel.refreshShot(pdir);
            const QVariantMap again = rowOf(pdir);
            ++lateRowsChecked;
            if (!shown.isEmpty() && shown == again) ++lateRowsSame;
            // the reading in the row: the launch monitor's figures among its metrics (any key the reading wrote)
            const QVariantMap metrics = shown.value(QStringLiteral("metrics")).toMap();
            bool reading = false;
            for (auto it = metrics.constBegin(); it != metrics.constEnd() && !reading; ++it)
                reading = it.key().startsWith(QLatin1String("lm")) || it.key().contains(QLatin1String("carry"), Qt::CaseInsensitive)
                          || it.key().contains(QLatin1String("ballSpeed"), Qt::CaseInsensitive);
            if (reading) ++lateRowsWithReading;
        }
        if (p.isSet(QStringLiteral("gc"))) engine.collectGarbage();
        diagBytes << QFileInfo(folder + QStringLiteral("/diagnostics.json")).size();
    }

    // ── the table: each step's ms per shot, and its growth ──
    QStringList paths;
    for (const StepTable &t : std::as_const(perShot))
        for (auto it = t.begin(); it != t.end(); ++it)
            if (!paths.contains(it.key())) paths << it.key();
    std::printf("\nstep (path; ms summed over its repeats in the shot)        shot1   mean2-6  mean%d-%d  slope ms/shot  repeats(last)\n",
                kShots - 4, kShots);
    for (const QString &path : std::as_const(paths)) {
        QList<double> y;
        for (const StepTable &t : std::as_const(perShot)) y << t.value(path).first;
        const auto mean = [&y](int a, int b) { double s = 0; int n = 0; for (int k = a; k <= b && k < y.size(); ++k) { s += y[k]; ++n; } return n ? s / n : 0.0; };
        std::printf("  %-56s %7.1f %8.1f %9.1f %10.2f %6d\n", qPrintable(path), y.value(0), mean(1, 5), mean(kShots - 5, kShots - 1),
                    slope(y.mid(1)), perShot.last().value(path).second);
    }
    std::printf("  late readings (J-46): %d rows checked; %d unchanged by a second refresh; %d show a launch-monitor figure\n",
                lateRowsChecked, lateRowsSame, lateRowsWithReading);
    std::printf("  diagnostics.json: %lld bytes after shot 10, %lld after shot %d\n", (long long)diagBytes.value(9), (long long)diagBytes.last(), kShots);

    if (p.isSet(QStringLiteral("csv"))) {
        QFile f(p.value(QStringLiteral("csv")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write("shot,diag_bytes");
            for (const QString &path : std::as_const(paths)) f.write(("," + path).toUtf8());
            f.write("\n");
            for (int i = 0; i < perShot.size(); ++i) {
                f.write(QByteArray::number(i + 1) + ',' + QByteArray::number(diagBytes.value(i)));
                for (const QString &path : std::as_const(paths)) f.write("," + QByteArray::number(perShot[i].value(path).first, 'f', 2));
                f.write("\n");
            }
        }
    }

    oldPanel.reset();
    SessionDiagnosticsModel::setShotCommands(nullptr);
    session->end();
    athlete.reset();
    pool.reset();
    return 0;
}
