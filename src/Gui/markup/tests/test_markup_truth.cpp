// Standalone round-trip test for markup_truth: read a swing's face-on geometry,
// read its existing truth.json into normalized labels, write it back through the
// production path. The byte-compatibility oracle is tools/swinglab score.py,
// driven by the sibling shell harness (run_markup_truth_test.sh).
//
// Build (Qt Core only, no GUI/OpenCV):
//   g++ -std=c++20 -fPIC test_markup_truth.cpp ../markup_truth.cpp \
//     -I$QT/include -I$QT/include/QtCore -L$QT/lib -lQt6Core \
//     -Wl,-rpath,$QT/lib -o /tmp/test_markup_truth

#include "../markup_truth.h"

#include <QString>
#include <cstdio>

using namespace pinpoint::markup;

int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <swing_dir>\n", argv[0]); return 2; }
    const QString swingDir = QString::fromLocal8Bit(argv[1]);

    const FaceOnInfo fo = readFaceOn(swingDir);
    if (!fo.ok) { std::fprintf(stderr, "readFaceOn failed for %s\n", argv[1]); return 1; }
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

    QString err;
    if (!writeTruth(swingDir, doc, fo, &err)) {
        std::fprintf(stderr, "writeTruth failed: %s\n", err.toLocal8Bit().constData());
        return 1;
    }
    const TruthSummary after = summarize(swingDir);
    std::printf("after:  exists=%d shaft=%d events=%d\n", after.exists, after.shaftCount, after.eventCount);
    std::printf("OK\n");
    return 0;
}
