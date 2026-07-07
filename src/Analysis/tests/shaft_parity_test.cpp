// Numeric parity gate: run the C++ v3.0-r1 decide core (shaft_track_assembly
// decideTrack, driven by the SAME FFV1 clip + anchors + skeleton the Python
// exemplar uses) and compare θ / kind against a Python `_v3_track.csv` generated
// on the SAME host. FFV1 is lossless so both sides decode bit-identical frames;
// the only differences are cross-language float. Gate: median |Δθ| on measured
// frames ≤ 0.5°, kind agreement ≥ 97%. SKIPs (exit 0) when the fixture is absent.
//
// Fixture (defaults shown; override via env):
//   PP_SHAFT_PARITY_DIR   /mnt/swingdata/shaftlab/lab/tape_20260705/s01
//       (anchors.csv, skeleton.csv, clipmeta.json, faceon_swing.avi)
//   PP_SHAFT_PARITY_REF   $DIR/out/faceon_swing_v3_track.csv  (the Python output)
//   PP_SHAFT_PARITY_CLUBS /mnt/swingdata/shaftlab/clubs.json
//   PP_SHAFT_PARITY_CLUB  "7 IRON"

#include "../shaft_track_assembly.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

using namespace pinpoint::analysis;

static QString env(const char *k, const QString &def)
{
    const char *v = std::getenv(k);
    return (v && *v) ? QString::fromUtf8(v) : def;
}
static double wrapDeg(double a) { a = std::fmod(a + 180.0, 360.0); if (a < 0) a += 360.0; return a - 180.0; }

int main()
{
    const QString dir   = env("PP_SHAFT_PARITY_DIR", "/mnt/swingdata/shaftlab/lab/tape_20260705/s01");
    const QString ref   = env("PP_SHAFT_PARITY_REF", dir + "/out/faceon_swing_v3_track.csv");
    const QString clubs = env("PP_SHAFT_PARITY_CLUBS", "/mnt/swingdata/shaftlab/clubs.json");
    const QString club  = env("PP_SHAFT_PARITY_CLUB", "7 IRON");
    const QString anchorsPath = dir + "/anchors.csv";
    const QString skelPath    = dir + "/skeleton.csv";
    const QString metaPath    = dir + "/clipmeta.json";
    const QString clipPath    = dir + "/faceon_swing.avi";

    for (const QString &p : {anchorsPath, skelPath, metaPath, clipPath, ref, clubs})
        if (!QFile::exists(p)) {
            std::printf("SKIP: fixture missing (%s)\n", p.toUtf8().constData());
            return 0;
        }

    // ── club record ──────────────────────────────────────────────────────────
    std::vector<double> bands; double lenMm = 940.0;
    {
        QFile f(clubs); f.open(QIODevice::ReadOnly);
        const QJsonObject rec = QJsonDocument::fromJson(f.readAll()).object().value(club).toObject();
        for (const QJsonValue &v : rec.value("bandCentersMm").toArray()) bands.push_back(v.toDouble());
        if (rec.contains("lengthMm")) lenMm = rec.value("lengthMm").toDouble();
    }

    // ── clip metadata (fps + t_us) ───────────────────────────────────────────
    double fps = 149.0; std::vector<int64_t> tUsMeta;
    {
        QFile f(metaPath); f.open(QIODevice::ReadOnly);
        const QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
        fps = m.value("fps").toDouble(fps);
        for (const QJsonValue &v : m.value("t_us").toArray()) tUsMeta.push_back(int64_t(v.toDouble()));
    }

    // ── decode all clip frames to grey (FFV1 lossless) ──────────────────────
    std::vector<cv::Mat> cache;
    {
        cv::VideoCapture cap(clipPath.toStdString());
        if (!cap.isOpened()) { std::printf("SKIP: cannot open clip %s\n", clipPath.toUtf8().constData()); return 0; }
        cv::Mat fr;
        while (cap.read(fr)) {
            cv::Mat g; if (fr.channels() == 1) g = fr.clone(); else cv::cvtColor(fr, g, cv::COLOR_BGR2GRAY);
            cache.push_back(g);
        }
    }
    const int nf = int(cache.size());
    if (nf < 2) { std::printf("SKIP: clip has < 2 frames\n"); return 0; }
    const int W = cache[0].cols, H = cache[0].rows;

    // ── anchors.csv → gx, gy, phiRaw ────────────────────────────────────────
    std::vector<double> gx(nf, std::nan("")), gy(nf, std::nan("")), phiRaw(nf, std::nan(""));
    {
        QFile f(anchorsPath); f.open(QIODevice::ReadOnly);
        for (const QByteArray &lineB : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(lineB).trimmed();
            if (line.isEmpty()) continue;
            const QStringList c = line.split(',');
            if (c.size() < 3) continue;
            const int fr = c[0].toInt();
            if (fr < 0 || fr >= nf) continue;
            gx[fr] = c[1].toDouble(); gy[fr] = c[2].toDouble();
            const bool ok = c.size() >= 5 && c[4].toInt() != 0;
            phiRaw[fr] = ok ? c[3].toDouble() : std::nan("");
        }
    }

    // ── skeleton.csv → rawJoints[nf][8] ─────────────────────────────────────
    std::vector<std::vector<cv::Point2d>> rawJoints(nf, std::vector<cv::Point2d>(8, {0, 0}));
    {
        QFile f(skelPath); f.open(QIODevice::ReadOnly);
        for (const QByteArray &lineB : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(lineB).trimmed();
            if (line.isEmpty()) continue;
            const QStringList c = line.split(',');
            if (c.size() < 1 + 8 * 3) continue;
            const int fr = c[0].toInt();
            if (fr < 0 || fr >= nf) continue;
            for (int j = 0; j < 8; ++j)
                rawJoints[fr][j] = {c[1 + 3 * j].toDouble(), c[2 + 3 * j].toDouble()};
        }
    }

    // t_us per frame (from clipmeta if long enough, else index·dt)
    std::vector<int64_t> tUs(nf);
    const int64_t dtUs = int64_t(1e6 / fps);
    for (int i = 0; i < nf; ++i) tUs[i] = (i < int(tUsMeta.size())) ? tUsMeta[i] : int64_t(i) * dtUs;

    // ── run the C++ decide core (defaults: geometric C2 + span-bound + ψ-rail)
    const FrameSource frameAt = [&](int i) -> cv::Mat { return (i >= 0 && i < nf) ? cache[i] : cv::Mat(); };
    ShaftV3Config cfg;
    ShaftDecideTrace trace;
    // Frames are pre-cached, so this times the COMPUTE only (scene bg + span
    // evidence + DP + reconcile) — decode is free live via the ring, matching
    // the production post-shot path (<3s target).
    const auto t0 = std::chrono::steady_clock::now();
    const ShaftTrack2D track = decideTrack(frameAt, tUs, gx, gy, phiRaw, rawJoints, W, H, fps,
                                           bands, lenMm, /*impactFrame=*/-1, cfg, &trace);
    const double computeMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - t0).count();

    // ── reference _v3_track.csv → frame → (theta, kind) ─────────────────────
    std::map<int, std::pair<double, QString>> refMap;
    {
        QFile f(ref); f.open(QIODevice::ReadOnly);
        bool header = true;
        for (const QByteArray &lineB : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(lineB).trimmed();
            if (line.isEmpty()) continue;
            if (header) { header = false; continue; }
            const QStringList c = line.split(',');
            if (c.size() < 6) continue;
            refMap[c[0].toInt()] = { c[3].toDouble(), c[4] };
        }
    }

    // ── compare ──────────────────────────────────────────────────────────────
    static const char *kTierKind[] = { "pred", "meas", "meas", "pred" };  // pred/ray/band/recon
    std::vector<double> dAll, dMeas, dSwing;
    int compared = 0, kindAgree = 0, missing = 0;
    for (size_t k = 0; k < trace.frameIdx.size(); ++k) {
        const int fr = trace.frameIdx[k];
        auto it = refMap.find(fr);
        if (it == refMap.end()) { ++missing; continue; }
        ++compared;
        const double d = std::abs(wrapDeg(trace.thetaDeg[k] - it->second.first));
        dAll.push_back(d);
        const QString cppKind = kTierKind[trace.tier[k]];
        if (cppKind == it->second.second) ++kindAgree;
        if (cppKind == "meas") dMeas.push_back(d);
        const SwingPhase ph = trace.phases.phase[size_t(fr)];
        if (ph == SwingPhase::Backswing || ph == SwingPhase::Top || ph == SwingPhase::Downswing
            || ph == SwingPhase::Impact || ph == SwingPhase::Thru)
            dSwing.push_back(d);
    }

    auto median = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    auto pct = [](std::vector<double> v, double p) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, size_t(p / 100.0 * v.size()))]; };

    const double medMeas = median(dMeas), medSwing = median(dSwing), medAll = median(dAll);
    const double p90Meas = pct(dMeas, 90), p90Swing = pct(dSwing, 90);
    const double kindFrac = compared ? double(kindAgree) / compared : 0.0;

    std::printf("=== shaft v3 C++↔Python parity (s01) ===\n");
    std::printf("  frames: cpp-emitted %zu, ref %zu, compared %d, missing-in-ref %d\n",
                trace.frameIdx.size(), refMap.size(), compared, missing);
    std::printf("  chir=%d span[%d,%d] heavy=%d  landmarks bs0=%d top=%d impact=%d fin0=%d\n",
                trace.chir, trace.spanLo, trace.spanHi, trace.heavyFrames,
                trace.phases.bs0, trace.phases.top, trace.phases.impact, trace.phases.fin0);
    std::printf("  |Δθ| median: all=%.3f°  meas=%.3f° (p90 %.3f°, n=%zu)  swing=%.3f° (p90 %.3f°, n=%zu)\n",
                medAll, medMeas, p90Meas, dMeas.size(), medSwing, p90Swing, dSwing.size());
    std::printf("  kind agreement: %.1f%% (%d/%d)\n", 100.0 * kindFrac, kindAgree, compared);
    std::printf("  track.valid=%d coverage=%.2f\n", track.valid, track.coverage);
    std::printf("  compute (decode-free, %d heavy frames): %.0f ms\n", trace.heavyFrames, computeMs);

    int fail = 0;
    auto check = [&](bool c, const char *label) { std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label); if (!c) ++fail; };
    check(compared > 100, "compared > 100 frames");
    check(medMeas <= 0.5, "median |Δθ| on measured frames ≤ 0.5°");
    check(medSwing <= 1.0, "median |Δθ| on swing frames ≤ 1.0°");
    check(kindFrac >= 0.97, "kind agreement ≥ 97%");

    std::printf("\n%s (%d failures)\n", fail ? "FAIL" : "PASS", fail);
    return fail;
}
