// Standalone round-trip test for markup_truth.
//
// Two halves:
//   1. SELF-CONTAINED CASES (always run, no arguments) — synthetic swing.json
//      trees in a temp dir covering stream selection for both cameras and the
//      truth.json / truth_dtl.json sidecars. This is what ctest runs.
//   2. A REAL SWING round-trip (only when a swing dir is given on the command
//      line) — read its face-on geometry, read its existing truth.json into
//      normalized labels, write it back through the production path. ⚠ This half
//      REWRITES that swing's truth.json, so it is opt-in. The byte-compatibility
//      oracle is tools/swinglab score.py.
//
// Build (Qt Core only, no GUI/OpenCV):
//   g++ -std=c++20 -fPIC test_markup_truth.cpp ../markup_truth.cpp \
//     -I$QT/include -I$QT/include/QtCore -L$QT/lib -lQt6Core \
//     -Wl,-rpath,$QT/lib -o /tmp/test_markup_truth

#include "../markup_truth.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

using namespace pinpoint::markup;

static int g_fails = 0;

static bool check(bool ok, const char *what)
{
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++g_fails;
    return ok;
}

// ── synthetic swing.json ─────────────────────────────────────────────────────
// One video stream: `setup.perspective` is omitted when < 0, which is how the
// 2026-06-11 session's swings actually look (no setup block at all).
static QJsonObject videoStream(const QString &alias, const QString &file, int w, int h,
                               int perspective, int nFrames, qint64 t0Us, qint64 stepUs)
{
    QJsonObject s;
    s.insert(QStringLiteral("kind"),  QStringLiteral("video"));
    s.insert(QStringLiteral("alias"), alias);
    s.insert(QStringLiteral("file"),  file);
    s.insert(QStringLiteral("source"),
             QJsonObject{ { QStringLiteral("width"), w }, { QStringLiteral("height"), h } });
    s.insert(QStringLiteral("playback"), QJsonObject{ { QStringLiteral("fps"), 30.0 } });
    if (perspective >= 0)
        s.insert(QStringLiteral("setup"), QJsonObject{ { QStringLiteral("perspective"), perspective } });
    QJsonArray t;
    for (int i = 0; i < nFrames; ++i) t.append(t0Us + qint64(i) * stepUs);
    s.insert(QStringLiteral("frames"), QJsonObject{ { QStringLiteral("t_us"), t } });
    return s;
}

static QString makeSwing(const QString &root, const QString &name, const QJsonArray &streams)
{
    const QString dir = QDir(root).filePath(name);
    QDir().mkpath(dir);
    QJsonObject swing;
    swing.insert(QStringLiteral("streams"), streams);
    QFile f(QDir(dir).filePath(QStringLiteral("swing.json")));
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(QJsonDocument(swing).toJson(QJsonDocument::Indented));
    return dir;
}

static QByteArray slurp(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// The two cameras share the window clock but sit a few ms out of phase, so the
// DTL stream is offset by 3 ms — a frame index carried straight across would land
// on the wrong instant, which is exactly what the nearest-t_us mapping prevents.
static const qint64 kFaceStepUs = 1000000 / 240;   // 240 fps face-on
static const qint64 kDtlStepUs  = 1000000 / 240;
static const qint64 kDtlPhaseUs = 3000;            // DTL exposures 3 ms later

static void selfTests()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::fprintf(stderr, "no temp dir\n"); ++g_fails; return; }
    const QString root = tmp.path();

    // (a) Both cameras declared by perspective (2 = face-on, 1 = down-the-line).
    const QString both = makeSwing(root, QStringLiteral("both"), QJsonArray{
        videoStream(QStringLiteral("Face-On"), QStringLiteral("face.mp4"), 1280, 1024, 2, 100, 0, kFaceStepUs),
        videoStream(QStringLiteral("Cam2"),    QStringLiteral("cam2.mp4"),  512, 1024, 1, 100, kDtlPhaseUs, kDtlStepUs) });

    std::printf("stream selection\n");
    const VideoStreamInfo face = readVideoStream(both, MarkupView::FaceOn);
    const VideoStreamInfo dtl  = readVideoStream(both, MarkupView::DownTheLine);
    check(face.ok && face.srcWidth == 1280 && face.srcHeight == 1024, "face-on by perspective==2");
    check(dtl.ok && dtl.srcWidth == 512 && dtl.srcHeight == 1024 && dtl.alias == QLatin1String("Cam2"),
          "DTL by perspective==1");
    check(readFaceOn(both).videoFile == QLatin1String("face.mp4"), "readFaceOn wrapper unchanged");

    // (b) The legacy session: no setup block anywhere, so the name decides. The
    //     face-on pick (alias contains "Face") must never be handed back as DTL.
    const QString legacy = makeSwing(root, QStringLiteral("legacy"), QJsonArray{
        videoStream(QStringLiteral("Face-On"),  QStringLiteral("face.mp4"),      1280, 1024, -1, 60, 0, kFaceStepUs),
        videoStream(QStringLiteral("Camera 2"), QStringLiteral("down-the-line.mp4"), 576, 1024, -1, 60, kDtlPhaseUs, kDtlStepUs) });
    const VideoStreamInfo legacyDtl = readVideoStream(legacy, MarkupView::DownTheLine);
    check(legacyDtl.ok && legacyDtl.srcWidth == 576, "DTL by file basename 'down-the-line'");
    check(readVideoStream(legacy, MarkupView::FaceOn).videoFile == QLatin1String("face.mp4"),
          "legacy face-on still the Face alias");

    const QString aliased = makeSwing(root, QStringLiteral("aliased"), QJsonArray{
        videoStream(QStringLiteral("Face-On"), QStringLiteral("a.mp4"), 1280, 1024, -1, 60, 0, kFaceStepUs),
        videoStream(QStringLiteral("DTL"),     QStringLiteral("b.mp4"),  512, 1024, -1, 60, 0, kDtlStepUs) });
    check(readVideoStream(aliased, MarkupView::DownTheLine).alias == QLatin1String("DTL"),
          "DTL by alias token");

    // (c) A face-on-only swing has no DTL camera, and a near-miss name is not one.
    const QString solo = makeSwing(root, QStringLiteral("solo"), QJsonArray{
        videoStream(QStringLiteral("Face-On"), QStringLiteral("face.mp4"), 1280, 1024, 2, 60, 0, kFaceStepUs) });
    check(!readVideoStream(solo, MarkupView::DownTheLine).ok, "face-on-only swing has no DTL");

    const QString nearMiss = makeSwing(root, QStringLiteral("nearmiss"), QJsonArray{
        videoStream(QStringLiteral("Face-On"),  QStringLiteral("face.mp4"),     1280, 1024, 2, 60, 0, kFaceStepUs),
        videoStream(QStringLiteral("shutdown"), QStringLiteral("midtl_b.mp4"),   512, 1024, -1, 60, 0, kDtlStepUs) });
    check(!readVideoStream(nearMiss, MarkupView::DownTheLine).ok, "'shutdown'/'midtl' are not DTL tokens");

    // ── the DTL sidecar ──────────────────────────────────────────────────────
    std::printf("truth_dtl.json\n");

    // Lay a face-on truth.json down first: marking DTL must not disturb one byte.
    TruthDoc faceDoc;
    ShaftLabel fl; fl.gripNx = 0.40; fl.gripNy = 0.30; fl.headNx = 0.55; fl.headNy = 0.80;
    faceDoc.shaft.insert(10, fl);
    faceDoc.events.insert(QStringLiteral("p7"), 10);
    faceDoc.meta.scope = QStringLiteral("full");
    QString err;
    check(writeTruth(both, faceDoc, face, MarkupView::FaceOn, &err), "write face-on truth.json");
    const QByteArray faceBytes = slurp(QDir(both).filePath(QStringLiteral("truth.json")));

    TruthDoc dtlDoc;
    ShaftLabel dl; dl.gripNx = 0.50; dl.gripNy = 0.25; dl.headNx = 0.62; dl.headNy = 0.90;
    dtlDoc.shaft.insert(12, dl);
    dtlDoc.ball.nx = 0.48; dtlDoc.ball.ny = 0.86; dtlDoc.ball.has = true;
    dtlDoc.events.insert(QStringLiteral("p7"), 12);          // must NOT be written
    dtlDoc.meta.club = QStringLiteral("7-IRON");             // must NOT be written
    check(writeTruth(both, dtlDoc, dtl, MarkupView::DownTheLine, &err), "write truth_dtl.json");

    check(slurp(QDir(both).filePath(QStringLiteral("truth.json"))) == faceBytes,
          "truth.json byte-identical after a DTL write");

    const QJsonObject dtlJson = QJsonDocument::fromJson(
        slurp(QDir(both).filePath(QStringLiteral("truth_dtl.json")))).object();
    check(dtlJson.value(QStringLiteral("view")).toString() == QLatin1String("DownTheLine"),
          "DTL file carries view=DownTheLine");
    check(dtlJson.value(QStringLiteral("stream")).toString() == QLatin1String("Cam2"),
          "DTL file names its stream");
    check(dtlJson.value(QStringLiteral("frame")).toObject().value(QStringLiteral("w")).toInt() == 512
       && dtlJson.value(QStringLiteral("frame")).toObject().value(QStringLiteral("h")).toInt() == 1024,
          "DTL file carries its frame size");
    check(!dtlJson.contains(QStringLiteral("events")) && !dtlJson.contains(QStringLiteral("meta")),
          "DTL file has no events and no meta");

    // Pixels are the DTL stream's, not face-on's: 0.50 * 512 = 256, never 640.
    const QJsonObject e0 = dtlJson.value(QStringLiteral("shaft")).toArray().at(0).toObject();
    const QJsonArray  g0 = e0.value(QStringLiteral("grip")).toArray();
    check(std::abs(g0.at(0).toDouble() - 0.50 * 512) < 1e-6
       && std::abs(g0.at(1).toDouble() - 0.25 * 1024) < 1e-6, "grip in DTL pixels (512 wide)");
    check(e0.value(QStringLiteral("t_us")).toInteger() == dtl.frameTimesUs[12],
          "t_us is the DTL stream's own frame time");
    const QJsonArray ballArr = dtlJson.value(QStringLiteral("ball")).toArray();
    check(ballArr.size() == 2 && std::abs(ballArr.at(0).toDouble() - 0.48 * 512) < 1e-6,
          "ball in DTL pixels");

    // Round trip back to normalized.
    const TruthDoc dtlBack = readTruth(both, dtl, MarkupView::DownTheLine);
    check(dtlBack.shaft.size() == 1 && dtlBack.shaft.contains(12)
       && std::abs(dtlBack.shaft.value(12).gripNx - 0.50) < 1e-6
       && std::abs(dtlBack.shaft.value(12).headNy - 0.90) < 1e-6, "DTL round trip");
    check(dtlBack.ball.has && std::abs(dtlBack.ball.nx - 0.48) < 1e-6, "DTL ball round trip");
    check(dtlBack.events.isEmpty() && dtlBack.meta.isEmpty(), "DTL read carries no events/meta");

    // The face-on side is untouched and still reads as itself.
    const TruthDoc faceBack = readTruth(both, face, MarkupView::FaceOn);
    check(faceBack.shaft.size() == 1 && faceBack.events.value(QStringLiteral("p7"), -1) == 10
       && faceBack.meta.scope == QLatin1String("full"), "face-on truth unchanged");

    // A truth_dtl.json is never readable as face-on truth: copy it over truth.json
    // in a scratch swing and the face-on read must refuse it outright.
    const QString trap = makeSwing(root, QStringLiteral("trap"), QJsonArray{
        videoStream(QStringLiteral("Face-On"), QStringLiteral("face.mp4"), 1280, 1024, 2, 100, 0, kFaceStepUs) });
    QFile::copy(QDir(both).filePath(QStringLiteral("truth_dtl.json")),
                QDir(trap).filePath(QStringLiteral("truth.json")));
    const VideoStreamInfo trapFace = readVideoStream(trap, MarkupView::FaceOn);
    const TruthDoc trapped = readTruth(trap, trapFace, MarkupView::FaceOn);
    check(trapped.shaft.isEmpty() && !trapped.ball.has, "DTL sidecar refused as face-on truth");
    check(!summarize(trap, MarkupView::FaceOn).exists, "summarize refuses the mismatched view");

    // ── the shared playhead: one instant, two streams ────────────────────────
    // The panes scrub together off ONE playhead in window µs. The face-on stream
    // is the master (a step is a face-on frame); the DTL pane shows the frame
    // whose t_us is NEAREST that instant. Never the same index — the cameras are
    // ~3 ms out of phase and need not even have the same frame count.
    std::printf("playhead mapping\n");
    const int faceIdx = 40;
    const int mapped  = frameIndexForUs(dtl, face.frameTimesUs[faceIdx]);
    check(std::llabs(dtl.frameTimesUs[mapped] - face.frameTimesUs[faceIdx]) <= kDtlStepUs / 2,
          "nearest DTL frame is within half a frame of the face-on instant");
    check(frameIndexForUs(face, dtl.frameTimesUs[mapped]) == faceIdx, "and maps back");

    // Unequal frame counts and a phase offset, checked against brute force: for
    // every face-on frame the mapping must return the true argmin of |Δt|, and
    // past the end of the shorter stream it must clamp to its last frame rather
    // than run off or wrap.
    const QString ragged = makeSwing(root, QStringLiteral("ragged"), QJsonArray{
        videoStream(QStringLiteral("Face-On"), QStringLiteral("f.mp4"), 1280, 1024, 2,
                    100, 0, kFaceStepUs),
        videoStream(QStringLiteral("DTL"),     QStringLiteral("d.mp4"),  512, 1024, 1,
                    /*nFrames=*/97, kDtlPhaseUs, kDtlStepUs) });
    const VideoStreamInfo rf = readVideoStream(ragged, MarkupView::FaceOn);
    const VideoStreamInfo rd = readVideoStream(ragged, MarkupView::DownTheLine);
    check(rf.frameCount() == 100 && rd.frameCount() == 97, "streams differ in length");

    bool allNearest = true, clamped = true;
    for (int i = 0; i < rf.frameCount(); ++i) {
        const qint64 t = rf.frameTimesUs[i];
        int best = 0;
        qint64 bestD = std::llabs(rd.frameTimesUs[0] - t);
        for (int j = 1; j < rd.frameCount(); ++j) {
            const qint64 d = std::llabs(rd.frameTimesUs[j] - t);
            if (d < bestD) { bestD = d; best = j; }
        }
        const int got = frameIndexForUs(rd, t);
        if (got != best) allNearest = false;
        if (got < 0 || got >= rd.frameCount()) clamped = false;
        // Past the DTL stream's last exposure the mapping must sit on its end.
        if (t > rd.frameTimesUs.last() && got != rd.frameCount() - 1) clamped = false;
    }
    check(allNearest, "every face-on frame maps to the true nearest DTL frame");
    check(clamped,    "and stays in range, clamping past the shorter stream's end");

    // The phase offset is real and the mapping absorbs it: index N is NOT the
    // same instant in both streams, but the mapped frame is always within half a
    // DTL frame interval.
    check(rd.frameTimesUs[10] - rf.frameTimesUs[10] == kDtlPhaseUs, "the 3 ms phase is there");
    // Inside the DTL stream's own span. Outside it — the 3 ms before its first
    // exposure, and anything past its last — the nearest frame is simply the end
    // one, which is the right answer and not a half-interval pairing.
    bool withinHalf = true;
    for (int i = 0; i < rf.frameCount(); ++i) {
        const qint64 t = rf.frameTimesUs[i];
        if (t < rd.frameTimesUs.first() || t > rd.frameTimesUs.last()) continue;
        const int j = frameIndexForUs(rd, t);
        if (std::llabs(rd.frameTimesUs[j] - t) > kDtlStepUs / 2 + 1) withinHalf = false;
    }
    check(withinHalf, "pairing is within half a DTL frame across the DTL span");
}

static int realSwingRoundTrip(const QString &swingDir)
{
    const FaceOnInfo fo = readFaceOn(swingDir);
    if (!fo.ok) { std::fprintf(stderr, "readFaceOn failed for %s\n", qPrintable(swingDir)); return 1; }

    // What the second camera resolves to on this real swing (ok=0 simply means
    // the swing was captured face-on only).
    const VideoStreamInfo dtl = readVideoStream(swingDir, MarkupView::DownTheLine);
    std::printf("dtl:    ok=%d alias=%s file=%s src=%dx%d frames=%d\n",
                dtl.ok, dtl.alias.toLocal8Bit().constData(),
                dtl.videoFile.toLocal8Bit().constData(),
                dtl.srcWidth, dtl.srcHeight, dtl.frameCount());
    std::printf("faceOn: alias=%s file=%s src=%dx%d frames=%d t0=%lld t_last=%lld\n",
                fo.alias.toLocal8Bit().constData(), fo.videoFile.toLocal8Bit().constData(),
                fo.srcWidth, fo.srcHeight, fo.frameCount(),
                (long long)(fo.frameTimesUs.isEmpty() ? 0 : fo.frameTimesUs.first()),
                (long long)(fo.frameTimesUs.isEmpty() ? 0 : fo.frameTimesUs.last()));

    const TruthSummary before = summarize(swingDir);
    std::printf("before: exists=%d shaft=%d events=%d\n", before.exists, before.shaftCount, before.eventCount);

    // Round-trip: existing truth.json -> normalized TruthDoc -> back to truth.json.
    const TruthDoc doc = readTruth(swingDir, fo);
    std::printf("parsed: shaft=%lld events=%lld\n",
                (long long)doc.shaft.size(), (long long)doc.events.size());
    for (auto it = doc.events.constBegin(); it != doc.events.constEnd(); ++it)
        std::printf("  event %-9s -> frame %d (t=%.3fs)\n", it.key().toLocal8Bit().constData(),
                    it.value(), double(fo.frameTimesUs[it.value()] - fo.frameTimesUs.first()) / 1e6);

    // Capture-conditions meta round-trip: stamp it onto the parsed doc, write,
    // read back, and verify it survives (additive "meta" block).
    TruthDoc stamped = doc;
    stamped.meta.lighting = QStringLiteral("bright");
    stamped.meta.shaft    = QStringLiteral("steel");
    stamped.meta.club     = QStringLiteral("7-IRON");
    stamped.meta.scope    = QStringLiteral("pitch");
    stamped.meta.tempo    = QStringLiteral("slow");
    stamped.meta.contact  = QStringLiteral("air");
    stamped.meta.clubLeavesFrame = true;
    // Stationary ball centre (additive "ball" [px,py]).
    stamped.ball.nx  = 0.5123;
    stamped.ball.ny  = 0.9210;
    stamped.ball.has = true;

    QString err;
    if (!writeTruth(swingDir, stamped, fo, &err)) {
        std::fprintf(stderr, "writeTruth failed: %s\n", err.toLocal8Bit().constData());
        return 1;
    }
    const TruthSummary after = summarize(swingDir);
    std::printf("after:  exists=%d shaft=%d events=%d\n", after.exists, after.shaftCount, after.eventCount);

    const TruthDoc reread = readTruth(swingDir, fo);
    std::printf("meta:   lighting=%s shaft=%s club=%s scope=%s tempo=%s contact=%s leavesFrame=%d\n",
                reread.meta.lighting.toLocal8Bit().constData(),
                reread.meta.shaft.toLocal8Bit().constData(),
                reread.meta.club.toLocal8Bit().constData(),
                reread.meta.scope.toLocal8Bit().constData(),
                reread.meta.tempo.toLocal8Bit().constData(),
                reread.meta.contact.toLocal8Bit().constData(),
                reread.meta.clubLeavesFrame);
    if (reread.meta.lighting != QLatin1String("bright")
        || reread.meta.shaft != QLatin1String("steel")
        || reread.meta.club != QLatin1String("7-IRON")
        || reread.meta.scope != QLatin1String("pitch")
        || reread.meta.tempo != QLatin1String("slow")
        || reread.meta.contact != QLatin1String("air")
        || !reread.meta.clubLeavesFrame) {
        std::fprintf(stderr, "meta round-trip FAILED\n");
        return 1;
    }

    std::printf("ball:   has=%d nx=%.4f ny=%.4f\n", reread.ball.has, reread.ball.nx, reread.ball.ny);
    if (!reread.ball.has
        || std::abs(reread.ball.nx - 0.5123) > 1e-3
        || std::abs(reread.ball.ny - 0.9210) > 1e-3) {
        std::fprintf(stderr, "ball round-trip FAILED\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}

int main(int argc, char **argv)
{
    selfTests();
    if (g_fails) { std::fprintf(stderr, "%d case(s) FAILED\n", g_fails); return 1; }

    // Opt-in: a real swing dir on the command line rewrites that swing's truth.json.
    if (argc >= 2) {
        const int rc = realSwingRoundTrip(QString::fromLocal8Bit(argv[1]));
        if (rc != 0) return rc;
    }
    std::printf("OK\n");
    return 0;
}
