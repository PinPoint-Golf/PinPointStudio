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

// review_views_shot_test — THE REPLAY AND THE MARKUP READ THROUGH THE SHOT (refactor 5.7c, 5.7d;
// build/refactor/step-4/LOCAL-COPIES.md rows 13 and 14). Over 5.4's owner rig (owner_rig.h): an Athlete on a temporary
// library holds a real Session and its Shots, and `app` is the door, as in the app.
//   REPLAY-FOLLOW   the replay follows the replayed folder's Shot (the composition root's edge)
//   REPLAY-EVT02    a real re-analysis (Shot::applyAnalysis) during a replay: the slice is read again through the Shot's
//                   RecordingSource and follows (the new impact), with no reload: no activeChanged, analysisDetailChanged once
//   REPLAY-DROP     a read still in flight when the replay stops is dropped
//   REPLAY-SOURCE   DiskReplaySource::applyAnalysis: the impact, the slice and the span follow; streams and playhead stay
//   MARKUP-SEEN     a truth written by another path (the Shot's own command) is what the markup tool's rows show, with no
//                   save of its own; its save is the Shot's (read back by Shot::markupTruth)
//   MARKUP-UNHELD   a swing no Session holds is not read, and its save is refused (N1 gone)
//   DATA-FOLLOW     the data viewer (refactor 5.8a) reads the Shot through the review views' door and, on a real
//                   re-analysis (Shot::applyAnalysis), its phases, segments and Properties follow with no reload
//   DATA-OWNERS     its Properties rows come from their owners: the Athlete's record, the Session's folder, the Shot
//   DATA-UNHELD     a folder the Session does not hold shows nothing (no file is read)
//   IMU-DOOR        (5.8a-imu) synthetic trunk (Witmotion), wG3 (HackMotion) and legacy recordings: the door's read gives
//                   each IMU lane's t, gyro z and quaternion exactly as the old viewer read them from the document, each
//                   stream's source and device blocks as stored, and the 3-D rig its inputs (analysis, origin, each
//                   stream's alias and encoded size, in order)
//   IMU-VIEW        the data viewer's IMU lanes per region (label, colour, ref), the span, and the Properties rows per
//                   stream (Kind, Serial, Rate, Fusion, Filter, Slot, Role, Samples) as the old viewer gave them, with
//                   the legacy lane's role from the settings' placement pairs and the session type
//
//   cmake --build build/tests --target review_views_shot_test -j 3
//   ctest --test-dir build/tests -R '^review_views_shot_test$' --output-on-failure

#include "../../Diagnostics/tests/owner_rig.h"

#include "../shot/shot_replay_controller.h"
#include "../shot/disk_replay_source.h"
#include "../markup/markup_controller.h"
#include "../markup/markup_truth.h"
#include "../review/swing_data_source.h"
#include "../review/shot_data_door_impl.h"
#include "../../Vocabulary/Analysis/swing_analysis.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/swing_doc.h"
#include "../../Buffer/swing_window.h"     // a LoadedRecording (the door's read of a Shot) is destroyed here

#include <QCoreApplication>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static void check(const QString &what, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) ++g_fail;
}

static QString fixture(const char *name) { return QStringLiteral(PP_LIVE_SWINGS_DIR "/%1/swing.json").arg(QLatin1String(name)); }

// The replay checks only that a stream's file exists before it queues it on a player.
static bool touchStreamFiles(const QString &dir)
{
    QFile doc(QDir(dir).filePath(QStringLiteral("swing.json")));
    if (!doc.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(doc.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QString f = v.toObject().value(QStringLiteral("file")).toString();
        if (f.isEmpty() || v.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
        QFile out(QDir(dir).filePath(f));
        if (!out.exists() && !out.open(QIODevice::WriteOnly)) return false;
    }
    return true;
}

static qint64 phaseUs(const QVariantMap &detail, analysis::Phase phase)
{
    for (const QVariant &v : detail.value(QStringLiteral("phases")).toList())
        if (v.toMap().value(QStringLiteral("phase")).toInt() == int(phase))
            return v.toMap().value(QStringLiteral("t_us")).toLongLong();
    return -1;
}

// A source of the test's own: no players (a media backend's own events would tear a real one down while the test waits
// on the lane). Its slice is the one projection, as DiskReplaySource's is.
class FakeSource final : public ReplaySource
{
public:
    QString     loaded;
    QVariantMap detail;
    int         applied = 0;
    bool load(const QString &dir, double, bool) override
    {
        loaded = dir;
        QFile f(QDir(dir).filePath(QStringLiteral("swing.json")));
        if (!f.open(QIODevice::ReadOnly)) return false;
        detail = analysisDetailForDocument(QJsonDocument::fromJson(f.readAll()).object());
        return true;
    }
    void unload() override { loaded.clear(); detail.clear(); }
    bool applyAnalysis(const QJsonObject &an, qint64 originUs) override
    {
        if (loaded.isEmpty()) return false;
        detail = analysisDetailFor(an, originUs);
        ++applied;
        return true;
    }
    int                       streamCount() const override { return loaded.isEmpty() ? 0 : 1; }
    QVector<ReplayStreamInfo> streams() const override { return {}; }
    QVariantMap               analysisDetail() const override { return detail; }
    qint64 startUs() const override { return 0; }
    qint64 endUs() const override { return 1; }
    qint64 impactUs() const override { return phaseUs(detail, analysis::Phase::Impact); }
    qint64 positionUs() const override { return 0; }
    bool   playing() const override { return false; }
    double speed() const override { return 0.25; }
    void setVideoSink(int, QVideoSink *) override {}
    void togglePlay() override {}
    void seekToFraction(double) override {}
    void seekToUs(qint64) override {}
    void stepFrame(int) override {}
    void setSpeed(double) override {}
    void beginScrub() override {}
    void endScrub() override {}
    bool impactLoopPlaying() const override { return true; }
    void toggleImpactLoop() override {}
    qint64 impactPositionUs() const override { return -1; }
    qint64 impactLoopStartUs() const override { return -1; }
    qint64 impactLoopEndUs() const override { return -1; }
    void seekImpactToUs(qint64) override {}
    void stepImpactFrame(int) override {}
};

// A re-analysis whose Address, Impact and Finish are at these window times (µs).
static WriteResult reanalyse(Shot *shot, qint64 addressUs, qint64 impactUs, qint64 finishUs)
{
    analysis::SwingAnalysis fresh;
    fresh.timeBase      = analysis::TimeBase::Window;
    fresh.score.overall = 41;
    for (const auto &[phase, t] : { std::pair{ analysis::Phase::Address, addressUs }, std::pair{ analysis::Phase::Impact, impactUs },
                                    std::pair{ analysis::Phase::Finish, finishUs } }) {
        analysis::PhaseEvent e;
        e.phase = phase;
        e.t_us  = t;
        fresh.phases.push_back(e);
    }
    AnalysisOutcome outcome;
    outcome.analysis = &fresh;
    return shot->applyAnalysis(outcome);
}

static void testReplay(owner_rig::ViewRig &rig, Session *s)
{
    std::printf("\nREPLAY — the slice follows the Shot\n");
    Shot *shot = rig.addLive(s, fixture("rich_7iron"));
    Shot *other = rig.addLive(s, fixture("rich_7iron"));
    check(QStringLiteral("two shots in a live session"), shot && other);
    if (!shot || !other) return;

    auto *fake = new FakeSource;
    ShotReplayController replay(std::unique_ptr<ReplaySource>(fake), nullptr);
    replay.setShotResolver([s](const QString &dir) { return s->shotAtFolder(dir); });
    QSignalSpy active(&replay, &ShotReplayController::activeChanged);
    QSignalSpy detail(&replay, &ShotReplayController::analysisDetailChanged);

    check(QStringLiteral("REPLAY-FOLLOW  the replay starts"), replay.start(shot->swingNumber(), shot->folder(), 0.25, false));
    check(QStringLiteral("REPLAY-FOLLOW  …and follows the replayed folder's Shot"), replay.followedShot() == shot);
    const qint64 before = phaseUs(replay.analysisDetail(), analysis::Phase::Impact);
    const qint64 impact = 1234567;   // window µs, not the fixture's
    check(QStringLiteral("REPLAY-FOLLOW  the slice at load is the document's (impact %1 µs)").arg(before),
          !replay.analysisDetail().value(QStringLiteral("phases")).toList().isEmpty() && before != impact);

    active.clear();
    detail.clear();
    const WriteResult w = reanalyse(shot, impact - 400000, impact, impact + 600000);
    check(QStringLiteral("REPLAY-EVT02  the Shot re-analysed (EVT-02)"), w.ok() && w.changed());
    check(QStringLiteral("REPLAY-EVT02  the read through the Shot settles"), replay.waitForAnalysisRead());
    check(QStringLiteral("REPLAY-EVT02  the impact follows (%1 µs)").arg(phaseUs(replay.analysisDetail(), analysis::Phase::Impact)),
          phaseUs(replay.analysisDetail(), analysis::Phase::Impact) == impact && replay.impactUs() == impact);
    check(QStringLiteral("REPLAY-EVT02  …in place: no reload, no activeChanged, analysisDetailChanged once"),
          fake->applied == 1 && active.isEmpty() && detail.count() == 1 && replay.active() && fake->loaded == shot->folder());

    std::printf("\nREPLAY-DROP — another shot's event, and a read the replay no longer wants\n");
    detail.clear();
    (void)reanalyse(other, 1000000, 1500000, 2000000);
    replay.waitForAnalysisRead();
    check(QStringLiteral("REPLAY-DROP  another shot's re-analysis is not read"), fake->applied == 1 && detail.isEmpty());
    emit shot->reanalysed(shot->ref());   // a read asked…
    replay.stop();                         // …and the replay stopped before it came back
    check(QStringLiteral("REPLAY-DROP  …it comes back"), replay.waitForAnalysisRead());
    check(QStringLiteral("REPLAY-DROP  a read in flight at a stop is dropped, and the Shot let go"),
          fake->applied == 1 && !replay.followedShot());
}

static void testReplaySource()
{
    std::printf("\nREPLAY-SOURCE — DiskReplaySource takes a slice in place\n");
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    const bool made = QFile::copy(fixture("rich_7iron"), QDir(dir).filePath(QStringLiteral("swing.json"))) && touchStreamFiles(dir);
    check(QStringLiteral("REPLAY-SOURCE  a swing folder"), made);
    if (!made) return;
    QFile doc(QDir(dir).filePath(QStringLiteral("swing.json")));
    (void)doc.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(doc.readAll()).object();

    DiskReplaySource src;
    // No event loop runs between the load and the checks: the empty media file's own events never arrive.
    check(QStringLiteral("REPLAY-SOURCE  loads (trimmed)"), src.load(dir, 0.25, true));
    const qint64 startUs = src.startUs(), endUs = src.endUs(), posUs = src.positionUs();
    const int streams = src.streamCount();
    const qint64 impact = src.impactUs() + 98765;
    QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    QJsonArray phases;
    for (const QJsonValue &v : an.value(QStringLiteral("phases")).toArray()) {
        QJsonObject p = v.toObject();
        if (p.value(QStringLiteral("phase")).toInt() == int(analysis::Phase::Impact))
            p[QStringLiteral("t_us")] = double(impact + analysisReadOriginUs(root));
        phases.append(p);
    }
    an[QStringLiteral("phases")] = phases;
    QSignalSpy span(&src, &ReplaySource::spanChanged);
    check(QStringLiteral("REPLAY-SOURCE  applyAnalysis takes it"), src.applyAnalysis(an, analysisReadOriginUs(root)));
    check(QStringLiteral("REPLAY-SOURCE  the impact and the slice follow (%1 µs)").arg(src.impactUs()),
          src.impactUs() == impact && phaseUs(src.analysisDetail(), analysis::Phase::Impact) == impact && span.count() == 1);
    check(QStringLiteral("REPLAY-SOURCE  the span, the streams and the playhead stay"),
          src.startUs() == startUs && src.endUs() == endUs && src.positionUs() == posUs && src.streamCount() == streams);
    src.unload();
    check(QStringLiteral("REPLAY-SOURCE  nothing loaded: nothing taken"), !src.applyAnalysis(an, 0));
}

static void testMarkup(owner_rig::ViewRig &rig, Session *s)
{
    std::printf("\nMARKUP — the truth is the Shot's\n");
    Shot *shot = rig.addLive(s, fixture("rich_7iron"));
    check(QStringLiteral("a shot"), shot && touchStreamFiles(shot->folder()));
    if (!shot) return;

    MarkupController mc;
    mc.setShotCommands(rig.app.get());
    mc.loadSwings({ shot->folder() });
    check(QStringLiteral("MARKUP-SEEN  the swing is open, unlabelled"), mc.hasSwing() && mc.labelledCount() == 0);

    // Another path: the Shot's own command, not the markup tool's save.
    const markup::VideoStreamInfo fo = markup::readVideoStream(shot->folder(), markup::MarkupView::FaceOn);
    markup::TruthDoc doc;
    doc.events.insert(markup::eventNames().first(), 10);
    const WriteResult w = shot->setMarkupTruth(TruthView::FaceOn, markup::toJson(doc, fo, markup::MarkupView::FaceOn));
    const QVariantMap row = mc.swings().value(0).toMap();
    check(QStringLiteral("MARKUP-SEEN  a truth written by another path is seen, with no save of the tool's (%1 event)")
              .arg(row.value(QStringLiteral("eventCount")).toInt()),
          w.ok() && mc.labelledCount() == 1 && row.value(QStringLiteral("labelled")).toBool()
              && row.value(QStringLiteral("eventCount")).toInt() == 1);

    mc.revert();   // the edit buffer, read again from the Shot
    mc.setEvent(markup::eventNames().last());
    const bool saved = mc.save();
    const markup::TruthSummary back = markup::summarizeJson(shot->markupTruth(TruthView::FaceOn));
    check(QStringLiteral("MARKUP-SEEN  the tool's save is the Shot's (%1 events read back by Shot::markupTruth)").arg(back.eventCount),
          saved && back.exists && back.eventCount == 2 && mc.labelledCount() == 1);

    std::printf("\nMARKUP-UNHELD — a swing no Session holds\n");
    QTemporaryDir tmp;
    const QString loose = QDir(tmp.path()).filePath(QStringLiteral("swing_0001"));
    QDir().mkpath(loose);
    const bool made = QFile::copy(fixture("rich_7iron"), QDir(loose).filePath(QStringLiteral("swing.json"))) && touchStreamFiles(loose)
                   && QFile::copy(QDir(shot->folder()).filePath(markup::truthFileName(markup::MarkupView::FaceOn)),
                                  QDir(loose).filePath(markup::truthFileName(markup::MarkupView::FaceOn)));
    mc.loadSwings({ loose });
    check(QStringLiteral("MARKUP-UNHELD  its truth file is not read (no Shot)"), made && mc.hasSwing() && mc.labelledCount() == 0);
    mc.setEvent(markup::eventNames().first());
    check(QStringLiteral("MARKUP-UNHELD  its save is refused"), !mc.save());
}

static QString propertyRow(const SwingDataSource &src, const QString &group, const QString &key)
{
    for (const dataview::PropertyGroup &g : src.metadata()) {
        if (g.group != group) continue;
        for (const dataview::PropertyRow &r : g.rows)
            if (r.k == key) return r.v;
    }
    return {};
}

static qint64 viewerPhaseUs(const SwingDataSource &src, analysis::Phase phase)
{
    for (const dataview::Phase &p : src.phases())
        if (p.phase == int(phase)) return p.t_us;
    return -1;
}

static void testDataViewer(owner_rig::ViewRig &rig, Session *s)
{
    std::printf("\nDATA — the data viewer is a presenter over the Shot (5.8a)\n");
    review::installShotDataResolver(review::makeShotDataResolver());
    Shot *shot = rig.addLive(s, fixture("rich_7iron"));
    check(QStringLiteral("DATA-FOLLOW  a shot in a live session"), shot != nullptr);
    if (!shot) return;

    SwingDataSource src;
    src.setSession(s);
    src.setSwingDir(shot->folder());
    check(QStringLiteral("DATA-FOLLOW  the read through the Shot settles"), src.waitForIdle());
    const qint64 before = viewerPhaseUs(src, analysis::Phase::Impact);
    check(QStringLiteral("DATA-FOLLOW  the viewer shows the shot (impact %1 µs, %2 lanes)").arg(before).arg(src.regionLaneCounts().value(0)),
          src.loaded() && before >= 0 && !src.segments().isEmpty());

    std::printf("\nDATA-OWNERS — each Properties row from its owner\n");
    check(QStringLiteral("DATA-OWNERS  Athlete = the Athlete's record (%1)").arg(propertyRow(src, QStringLiteral("Swing"), QStringLiteral("Athlete"))),
          propertyRow(src, QStringLiteral("Swing"), QStringLiteral("Athlete")) == s->athlete()->record().name);
    check(QStringLiteral("DATA-OWNERS  Session = the Session's folder (%1)").arg(propertyRow(src, QStringLiteral("Swing"), QStringLiteral("Session"))),
          propertyRow(src, QStringLiteral("Swing"), QStringLiteral("Session")) == s->entry()->identity.folderName());
    check(QStringLiteral("DATA-OWNERS  Swing = the Shot's folder (%1)").arg(propertyRow(src, QStringLiteral("Swing"), QStringLiteral("Swing"))),
          propertyRow(src, QStringLiteral("Swing"), QStringLiteral("Swing")) == QFileInfo(shot->folder()).fileName());
    (void)shot->rate(4);
    src.setSessionType(src.sessionType() + 1);   // re-derives from the cached read: no file read, no re-read
    src.waitForIdle();
    check(QStringLiteral("DATA-OWNERS  Rating = the Shot's review (%1)").arg(propertyRow(src, QStringLiteral("Review"), QStringLiteral("Rating"))),
          propertyRow(src, QStringLiteral("Review"), QStringLiteral("Rating")) == QStringLiteral("4 ★"));

    std::printf("\nDATA-FOLLOW — a real re-analysis of the shot on screen\n");
    QSignalSpy dirs(&src, &SwingDataSource::swingDirChanged);
    const int rev = src.revision();
    const qint64 impact = 1234567;
    const WriteResult w = reanalyse(shot, impact - 400000, impact, impact + 600000);
    check(QStringLiteral("DATA-FOLLOW  the Shot re-analysed (EVT-02)"), w.ok() && w.changed());
    check(QStringLiteral("DATA-FOLLOW  the read through the Shot settles"), src.waitForIdle());
    check(QStringLiteral("DATA-FOLLOW  the impact follows (%1 → %2 µs)").arg(before).arg(viewerPhaseUs(src, analysis::Phase::Impact)),
          viewerPhaseUs(src, analysis::Phase::Impact) == impact);
    check(QStringLiteral("DATA-FOLLOW  …and the score row (%1)").arg(propertyRow(src, QStringLiteral("Analysis"), QStringLiteral("Score"))),
          propertyRow(src, QStringLiteral("Analysis"), QStringLiteral("Score")) == QStringLiteral("41"));
    check(QStringLiteral("DATA-FOLLOW  …with no reload: the same folder, one new read (revision %1 → %2)").arg(rev).arg(src.revision()),
          dirs.isEmpty() && src.revision() == rev + 1 && src.swingDir() == shot->folder());

    std::printf("\nDATA-UNHELD — a folder the Session does not hold\n");
    QTemporaryDir other;
    const QString foreign = QDir(other.path()).filePath(QStringLiteral("swing_0001"));
    (void)QDir().mkpath(foreign);
    (void)QFile::copy(fixture("rich_7iron"), QDir(foreign).filePath(QStringLiteral("swing.json")));
    src.setSwingDir(foreign);
    src.waitForIdle();
    check(QStringLiteral("DATA-UNHELD  nothing is shown, and nothing read"), !src.loaded() && src.phases().isEmpty());
}

// ── IMU — the motion-sensor lanes through the door (refactor 5.8a-imu) ──────────────────────────────────────────────
// A synthetic recording: rich_7iron's camera and ball streams with IMU streams interleaved, in the shape the exporter
// writes (swing_exporter.cpp: kind "imu", alias, schema imu_sample_v2, source.serial, device.{outputRateHz, fusionMode,
// orientationFilter, role, roleName[, placementSlot]}, a HackMotion lane's "instrument": "hackmotion", samples.{count,
// t_us, data[accel3, gyro3, quat wxyz]}). Every value is a float's, as the exporter stores an ImuSample's.

static double f32(double v) { return double(float(v)); }

static QJsonObject imuStream(const QString &alias, const QString &serial, bool hackMotion, const QJsonObject &device,
                             qint64 t0, qint64 dt, int n, int seed)
{
    QJsonArray t, data;
    for (int i = 0; i < n; ++i) {
        const double th = (seed + 1) * 0.02 * i;   // a turn about z, so the fused rate is not zero
        t.append(double(t0 + i * dt));
        data.append(QJsonArray{ f32(0.01 * i), f32(-0.98 + 0.001 * seed), f32(0.05 * seed),
                                f32(seed * 3.5 - i * 0.25), f32(i * 0.75 - seed), f32(seed * 100.0 + i * 1.25),
                                f32(std::cos(th / 2)), f32(0.01 * seed), f32(-0.002 * i), f32(std::sin(th / 2)) });
    }
    QJsonObject s{ { QStringLiteral("kind"), QStringLiteral("imu") }, { QStringLiteral("alias"), alias },
                   { QStringLiteral("schema"), QStringLiteral("imu_sample_v2") },
                   { QStringLiteral("source"), QJsonObject{ { QStringLiteral("serial"), serial } } },
                   { QStringLiteral("samples"), QJsonObject{ { QStringLiteral("count"), n }, { QStringLiteral("t_us"), t },
                                                              { QStringLiteral("data"), data } } } };
    if (!device.isEmpty()) s[QStringLiteral("device")] = device;
    if (hackMotion) s[QStringLiteral("instrument")] = QStringLiteral("hackmotion");
    return s;
}

// rich_7iron with its streams replaced by `order` (the fixture's own video and ball streams spliced in where a bare
// {kind: video} and {kind: ball} stand), written to `path`.
static bool writeImuRecording(const QString &path, const QList<QJsonObject> &order)
{
    QFile in(fixture("rich_7iron"));
    if (!in.open(QIODevice::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
    QJsonObject video, ball;
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QString kind = v.toObject().value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("video") && video.isEmpty()) video = v.toObject();
        if (kind == QLatin1String("ball") && ball.isEmpty()) ball = v.toObject();
    }
    QJsonArray streams;
    for (const QJsonObject &s : order) {
        const QString kind = s.value(QStringLiteral("kind")).toString();
        streams.append(kind == QLatin1String("video") ? video : kind == QLatin1String("ball") ? ball : s);
    }
    root[QStringLiteral("streams")] = streams;
    QFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(root).toJson()) > 0;
}

static QJsonObject readDoc(const QString &swingDir)
{
    QFile f(QDir(swingDir).filePath(QStringLiteral("swing.json")));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

// THE OLD READ (SwingDataSource::reload at c0780dd5, read from the document): t from samples.t_us, gyro z = data[5],
// the quaternion = data[6..9]; min(t, data) rows, a row shorter than 10 skipped.
struct OldLane { QVector<qint64> t; QVector<double> gz; QVector<std::array<double, 4>> q; };
static OldLane oldLaneOf(const QJsonObject &st)
{
    OldLane o;
    const QJsonObject samples = st.value(QStringLiteral("samples")).toObject();
    const QJsonArray dataArr = samples.value(QStringLiteral("data")).toArray();
    const QJsonArray tArr    = samples.value(QStringLiteral("t_us")).toArray();
    const int count = std::min(tArr.size(), dataArr.size());
    for (int i = 0; i < count; ++i) {
        const QJsonArray row = dataArr.at(i).toArray();
        if (row.size() < 10) continue;
        o.t.push_back(qint64(tArr.at(i).toDouble()));
        o.gz.push_back(row.at(5).toDouble());
        o.q.push_back({ row.at(6).toDouble(), row.at(7).toDouble(), row.at(8).toDouble(), row.at(9).toDouble() });
    }
    return o;
}

// The door's read of the Shot, stream by stream, against the old read of its document: the IMU lanes' samples
// exactly, the blocks the Properties rows are made from, and the 3-D rig's inputs (the analysis, its origin, and each
// stream's alias and encoded size in the document's order: SwingRigDriver::prepare reads nothing else; it never read
// an IMU sample, old or new).
static void checkDoorRead(const QString &tag, Shot *shot)
{
    const QJsonObject doc = readDoc(shot->folder());
    const QJsonArray streams = doc.value(QStringLiteral("streams")).toArray();
    const review::ShotDataRead rd = review::shotDataOf(shot->recordingSource().read());
    check(QStringLiteral("%1  the door reads every stored stream, in order (%2 of %3)").arg(tag).arg(rd.streams.size()).arg(streams.size()),
          rd.ok && int(rd.streams.size()) == streams.size());
    if (!rd.ok || int(rd.streams.size()) != streams.size()) return;

    bool rig = rd.analysis == doc.value(QStringLiteral("analysis")).toObject() && rd.originUs == analysisReadOriginUs(doc);
    for (int i = 0; i < streams.size(); ++i) {
        const QJsonObject st = streams.at(i).toObject();
        const review::ShotDataStream &got = rd.streams[size_t(i)];
        rig = rig && got.kind == st.value(QStringLiteral("kind")).toString() && got.alias == st.value(QStringLiteral("alias")).toString()
              && got.encoded == st.value(QStringLiteral("encoded")).toObject();
        if (got.kind != QLatin1String("imu")) continue;
        const OldLane old = oldLaneOf(st);
        const bool same = got.imuLane && got.t == old.t && got.gyroZ == old.gz && got.quat == old.q;
        check(QStringLiteral("%1  lane \"%2\": t, gyro z and quaternion as the old read (%3 / %4 samples, t %5…%6 / %7…%8 µs, "
                             "gyro z %9 / %10 at the last)")
                  .arg(tag, got.alias).arg(got.t.size()).arg(old.t.size())
                  .arg(got.t.value(0)).arg(got.t.isEmpty() ? -1 : got.t.last())
                  .arg(old.t.value(0)).arg(old.t.isEmpty() ? -1 : old.t.last())
                  .arg(got.gyroZ.isEmpty() ? 0.0 : got.gyroZ.last()).arg(old.gz.isEmpty() ? 0.0 : old.gz.last()),
              same && !old.t.isEmpty());
        const QJsonObject sm = st.value(QStringLiteral("samples")).toObject();
        check(QStringLiteral("%1  lane \"%2\": its source, device and sample count as stored (serial %3, role %4, count %5)")
                  .arg(tag, got.alias, got.source.value(QStringLiteral("serial")).toString())
                  .arg(got.device.value(QStringLiteral("role")).toInt(-1)).arg(got.sampleCount),
              got.source == st.value(QStringLiteral("source")).toObject() && got.device == st.value(QStringLiteral("device")).toObject()
                  && got.hasSampleCount == sm.contains(QStringLiteral("count")) && got.sampleCount == sm.value(QStringLiteral("count")).toInt());
    }
    check(QStringLiteral("%1  the 3-D rig's inputs: the analysis, its origin (%2 µs), and each stream's alias and encoded size in order")
              .arg(tag).arg(rd.originUs),
          rig);
}

// The viewer's IMU chips in a region: "label|colorKey|ref; …".
static QString imuChips(SwingDataSource &src, const QString &region)
{
    src.setRegion(region);
    QStringList out;
    for (const dataview::SourceChip &c : src.resolvedSources())
        if (c.kind == QLatin1String("imu")) out << c.label + QLatin1Char('|') + c.colorKey + QLatin1Char('|') + c.ref;
    return out.join(QStringLiteral("; "));
}

// A Properties group's rows: "k=v; …" (in order), or "(none)".
static QString groupRows(const SwingDataSource &src, const QString &group)
{
    for (const dataview::PropertyGroup &g : src.metadata()) {
        if (g.group != group) continue;
        QStringList out;
        for (const dataview::PropertyRow &r : g.rows) out << r.k + QLatin1Char('=') + r.v;
        return out.join(QStringLiteral("; "));
    }
    return QStringLiteral("(none)");
}

static void expectEq(const QString &what, const QString &got, const QString &want)
{
    check(QStringLiteral("%1: \"%2\"%3").arg(what, got, got == want ? QString() : QStringLiteral(" — expected \"%1\"").arg(want)),
          got == want);
}

// A viewer on the shot, as the data viewer is bound (session, session type, the settings' placement pairs, swingDir).
static bool openViewer(SwingDataSource &src, Session *s, Shot *shot, int sessionType, const QStringList &pairs)
{
    src.setSession(s);
    src.setSessionType(sessionType);
    src.setImuPlacementPairs(pairs);
    src.setSwingDir(shot->folder());
    return src.waitForIdle() && src.loaded();
}

static void testImuLanes(owner_rig::ViewRig &rig, Session *s)
{
    std::printf("\nIMU — the motion-sensor lanes through the door (5.8a-imu)\n");
    review::installShotDataResolver(review::makeShotDataResolver());
    QTemporaryDir tmp;
    const QJsonObject video{ { QStringLiteral("kind"), QStringLiteral("video") } };
    const QJsonObject ball{ { QStringLiteral("kind"), QStringLiteral("ball") } };
    auto device = [](int rateHz, const QString &fusion, const QString &filter, int role, const QString &roleName) {
        return QJsonObject{ { QStringLiteral("outputRateHz"), rateHz }, { QStringLiteral("fusionMode"), fusion },
                            { QStringLiteral("orientationFilter"), filter }, { QStringLiteral("role"), role },
                            { QStringLiteral("roleName"), roleName } };
    };
    using R = analysis::SegmentRole;

    // A TRUNK RECORDING (Witmotion pelvis + thorax): the pelvis lane starts before the first video frame (3173 µs), the
    // thorax lane runs past window.end_us (5 000 000), and one carries a legacy placementSlot.
    QJsonObject pelvisDev = device(200, QStringLiteral("host"), QStringLiteral("vqf"), int(R::Pelvis), QStringLiteral("Pelvis"));
    pelvisDev[QStringLiteral("placementSlot")] = QStringLiteral("P");
    const QString trunkPath = QDir(tmp.path()).filePath(QStringLiteral("trunk.json"));
    const bool trunkMade = writeImuRecording(trunkPath, {
        imuStream(QStringLiteral("Pelvis Sensor"), QStringLiteral("WT-PELVIS-01"), false, pelvisDev, 1000, 5000, 30, 1),
        video, ball,
        imuStream(QStringLiteral("Chest Sensor"), QStringLiteral("WT-THORAX-02"), false,
                  device(200, QStringLiteral("host"), QStringLiteral("vqf"), int(R::Thorax), QStringLiteral("Thorax")),
                  4900000, 5000, 40, 2) });
    // A HACKMOTION RECORDING (the wG3's lower-arm and palm units).
    QJsonObject hmDev = device(100, QStringLiteral("device"), QString(), int(R::LeadForearm), QStringLiteral("LeadForearm"));
    hmDev[QStringLiteral("calibrationStateAtStart")] = 2;
    QJsonObject palmDev = device(100, QStringLiteral("device"), QString(), int(R::LeadHand), QStringLiteral("LeadHand"));
    palmDev[QStringLiteral("calibrationStateAtStart")] = 2;
    const QString hmPath = QDir(tmp.path()).filePath(QStringLiteral("hm.json"));
    const bool hmMade = writeImuRecording(hmPath, {
        video,
        imuStream(QStringLiteral("HackMotion lower arm"), QStringLiteral("HM0042#lowerArm"), true, hmDev, 2000000, 10000, 25, 3),
        ball,
        imuStream(QStringLiteral("HackMotion palm"), QStringLiteral("HM0042#palm"), true, palmDev, 2003000, 10000, 24, 4) });
    // A LEGACY RECORDING: a lane with no device block (its role from the settings' placement and the session type).
    const QString legacyPath = QDir(tmp.path()).filePath(QStringLiteral("legacy.json"));
    const bool legacyMade = writeImuRecording(legacyPath, {
        video, ball, imuStream(QStringLiteral("Sensor A"), QStringLiteral("WT-LEGACY-A"), false, {}, 500000, 5000, 33, 5) });
    check(QStringLiteral("IMU  three synthetic recordings"), trunkMade && hmMade && legacyMade);
    if (!trunkMade || !hmMade || !legacyMade) return;

    Shot *trunk  = rig.addLive(s, trunkPath);
    Shot *hm     = rig.addLive(s, hmPath);
    Shot *legacy = rig.addLive(s, legacyPath);
    check(QStringLiteral("IMU  held as three shots of the live session"), trunk && hm && legacy);
    if (!trunk || !hm || !legacy) return;

    std::printf("\nIMU-DOOR — the door's read against the old read of the document\n");
    checkDoorRead(QStringLiteral("IMU-DOOR trunk "), trunk);
    checkDoorRead(QStringLiteral("IMU-DOOR wG3   "), hm);
    checkDoorRead(QStringLiteral("IMU-DOOR legacy"), legacy);

    std::printf("\nIMU-VIEW trunk — lanes and Properties as the old viewer gave them\n");
    {
        SwingDataSource src;
        check(QStringLiteral("IMU-VIEW trunk  the viewer shows the shot"), openViewer(src, s, trunk, 0, {}));
        expectEq(QStringLiteral("IMU-VIEW trunk  Axial lanes"), imuChips(src, QStringLiteral("Axial")),
                 QStringLiteral("Motion sensor · Pelvis|teal|Pelvis Sensor; Motion sensor · Thorax|blue|Chest Sensor"));
        expectEq(QStringLiteral("IMU-VIEW trunk  Lower lanes"), imuChips(src, QStringLiteral("Lower")),
                 QStringLiteral("Motion sensor · Pelvis|teal|Pelvis Sensor"));
        expectEq(QStringLiteral("IMU-VIEW trunk  Upper lanes"), imuChips(src, QStringLiteral("Upper")), QString());
        expectEq(QStringLiteral("IMU-VIEW trunk  Delivery lanes"), imuChips(src, QStringLiteral("Delivery")), QString());
        expectEq(QStringLiteral("IMU-VIEW trunk  best region"), src.bestRegion(), QStringLiteral("Axial"));
        expectEq(QStringLiteral("IMU-VIEW trunk  span (the thorax lane's last sample past window.end_us)"),
                 QString::number(src.spanUs()), QStringLiteral("5095000"));
        expectEq(QStringLiteral("IMU-VIEW trunk  Stream · Pelvis Sensor"), groupRows(src, QStringLiteral("Stream · Pelvis Sensor")),
                 QStringLiteral("Kind=imu; Serial=WT-PELVIS-01; Rate=200 Hz; Fusion=host; Filter=vqf; Slot=P; Role=Pelvis; Samples=30"));
        expectEq(QStringLiteral("IMU-VIEW trunk  Stream · Chest Sensor"), groupRows(src, QStringLiteral("Stream · Chest Sensor")),
                 QStringLiteral("Kind=imu; Serial=WT-THORAX-02; Rate=200 Hz; Fusion=host; Filter=vqf; Role=Thorax; Samples=40"));
    }

    std::printf("\nIMU-VIEW wG3 — lanes and Properties as the old viewer gave them\n");
    {
        SwingDataSource src;
        check(QStringLiteral("IMU-VIEW wG3    the viewer shows the shot"), openViewer(src, s, hm, 1, {}));
        const QString arm = QStringLiteral("Motion sensor · Lead forearm|purple|HackMotion lower arm; "
                                           "Motion sensor · Lead hand|coral|HackMotion palm");
        expectEq(QStringLiteral("IMU-VIEW wG3    Axial lanes"), imuChips(src, QStringLiteral("Axial")), QString());
        expectEq(QStringLiteral("IMU-VIEW wG3    Lower lanes"), imuChips(src, QStringLiteral("Lower")), QString());
        expectEq(QStringLiteral("IMU-VIEW wG3    Upper lanes"), imuChips(src, QStringLiteral("Upper")), arm);
        expectEq(QStringLiteral("IMU-VIEW wG3    Delivery lanes"), imuChips(src, QStringLiteral("Delivery")), arm);
        check(QStringLiteral("IMU-VIEW wG3    best region is an arm region (%1)").arg(src.bestRegion()),
              src.bestRegion() == QLatin1String("Upper") || src.bestRegion() == QLatin1String("Delivery"));
        expectEq(QStringLiteral("IMU-VIEW wG3    span (window.end_us)"), QString::number(src.spanUs()), QStringLiteral("5000000"));
        expectEq(QStringLiteral("IMU-VIEW wG3    Stream · HackMotion lower arm"), groupRows(src, QStringLiteral("Stream · HackMotion lower arm")),
                 QStringLiteral("Kind=imu; Serial=HM0042#lowerArm; Rate=100 Hz; Fusion=device; Filter=; Role=LeadForearm; Samples=25"));
        expectEq(QStringLiteral("IMU-VIEW wG3    Stream · HackMotion palm"), groupRows(src, QStringLiteral("Stream · HackMotion palm")),
                 QStringLiteral("Kind=imu; Serial=HM0042#palm; Rate=100 Hz; Fusion=device; Filter=; Role=LeadHand; Samples=24"));
    }

    std::printf("\nIMU-VIEW legacy — the role from the settings' placement and the session type\n");
    const QStringList pairs{ QStringLiteral("WT-LEGACY-A"), QStringLiteral("A") };
    {
        SwingDataSource src;   // a Wrist session (type 1): slot A is the lead forearm
        check(QStringLiteral("IMU-VIEW legacy the viewer shows the shot (Wrist, slot A)"), openViewer(src, s, legacy, 1, pairs));
        expectEq(QStringLiteral("IMU-VIEW legacy Upper lanes"), imuChips(src, QStringLiteral("Upper")),
                 QStringLiteral("Motion sensor · Lead forearm|purple|Sensor A"));
        expectEq(QStringLiteral("IMU-VIEW legacy Axial lanes (a role is known: no fallback)"), imuChips(src, QStringLiteral("Axial")), QString());
        expectEq(QStringLiteral("IMU-VIEW legacy Stream · Sensor A"), groupRows(src, QStringLiteral("Stream · Sensor A")),
                 QStringLiteral("Kind=imu; Serial=WT-LEGACY-A; Role=LeadForearm; Samples=33"));
    }
    {
        SwingDataSource src;   // another session type: no role, so every region shows the lane generically
        check(QStringLiteral("IMU-VIEW legacy the viewer shows the shot (type 0)"), openViewer(src, s, legacy, 0, pairs));
        const QString generic = QStringLiteral("Motion sensor · Sensor A|teal|Sensor A");
        expectEq(QStringLiteral("IMU-VIEW legacy Axial lanes (the generic fallback)"), imuChips(src, QStringLiteral("Axial")), generic);
        expectEq(QStringLiteral("IMU-VIEW legacy Upper lanes (the generic fallback)"), imuChips(src, QStringLiteral("Upper")), generic);
        expectEq(QStringLiteral("IMU-VIEW legacy Stream · Sensor A (no Role row)"), groupRows(src, QStringLiteral("Stream · Sensor A")),
                 QStringLiteral("Kind=imu; Serial=WT-LEGACY-A; Samples=33"));
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("review_views_shot_test\n");
    RuleData &rules = installProcessRuleData();
    if (!rules.snapshot() || !rules.snapshot()->pack) { check(QStringLiteral("the shipped pack is read"), false); return 1; }

    testReplaySource();   // first: no event loop has run yet

    QTemporaryDir tmp;
    {
        owner_rig::ViewRig rig(QDir(tmp.path()).filePath(QStringLiteral("lib/Review-Golfer")));
        Session *s = rig.startLive();
        check(QStringLiteral("a live session"), s != nullptr);
        if (s) {
            testReplay(rig, s);
            testMarkup(rig, s);
            testDataViewer(rig, s);
            testImuLanes(rig, s);
        }
    }
    std::printf("\n%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
