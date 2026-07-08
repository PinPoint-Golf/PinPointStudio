// Numeric parity gate: run the C++ TemporalBallTracker (ball_temporal.h) over the
// SAME DoG response stack R and baseline B the python exemplar (ball_state_machine.py)
// computed, and compare the locked position / launch frame / median novelty against
// the reference generated on the same host by tools/balllab/gen_parity_ref.py.
//
// WHY it consumes python's R rather than decoding the clip itself: independent
// OpenCV versions decode this H.264 into DIFFERENT pixels (measured: cv2 4.13 vs
// system 4.10 — frame-200 gray sums 43.50M vs 43.88M), so an end-to-end C++
// decode could never be byte-parity. Feeding python's exact R makes this a
// byte-exact test of the TRACKER STATE MACHINE given identical R + B — the oracle
// contract (bible §12). The DoG + padded-crop pipeline is verified separately, and
// byte-for-byte, by ball_temporal_test (paddedResponse vs the acceptance.py recipe).
//
// Fixture (defaults shown; override via env):
//   PP_BALL_PARITY_DIR   /tmp/ball_parity   (a dir of <tag>.json + <tag>.B.f32 + <tag>.R.f32)
//
// Generate it first (same host is irrelevant now — R is python's, not decoded here):
//   python tools/balllab/gen_parity_ref.py --out /tmp/ball_parity
//
// Gates (per fixture): lock/launch presence match, locked centre |Δ| ≤ 0.05 px,
// ix/iy exact, lock frame exact, launch frame exact, median novelty / L0 within 1%.
// SKIPs (exit 0) when the fixture dir is absent.

#include "../ball_temporal.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace pinpoint::balltemporal;

static QString env(const char *k, const QString &def)
{
    const char *v = std::getenv(k);
    return (v && *v) ? QString::fromUtf8(v) : def;
}

// Load a raw float32 blob (count floats) into a vector.
static bool loadF32(const QString &path, std::vector<float> &out, qint64 count)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray raw = f.readAll();
    if (raw.size() != count * qint64(sizeof(float))) return false;
    out.resize(size_t(count));
    std::memcpy(out.data(), raw.constData(), size_t(raw.size()));
    return true;
}

// Run one fixture. Returns number of failing checks (0 = pass); -1 = skipped.
static int runFixture(const QString &jsonPath, int &checksOut)
{
    QFile jf(jsonPath);
    if (!jf.open(QIODevice::ReadOnly)) { std::printf("  cannot read %s\n", jsonPath.toUtf8().constData()); return 1; }
    const QJsonObject o = QJsonDocument::fromJson(jf.readAll()).object();

    const QString tag  = QFileInfo(jsonPath).completeBaseName();
    const int bw = o.value("bw").toInt(), bh = o.value("bh").toInt();
    const int n  = o.value("n_frames").toInt();
    const double rHat = o.value("r_hat").toDouble(), fps = o.value("fps").toDouble();
    const double noise0 = o.value("noise0").toDouble();
    const int addressEnd = o.value("address_end_idx").toInt();
    const QJsonObject ref = o.value("ref").toObject();
    const QString dir = QFileInfo(jsonPath).absolutePath();

    std::printf("── %s  (roi %dx%d, r_hat=%.2f, fps=%.2f, sat=%.0f%%, n=%d)\n",
                tag.toUtf8().constData(), bw, bh, rHat, fps,
                100.0 * o.value("satFrac").toDouble(), n);

    std::vector<float> Bbuf, Rbuf;
    if (!loadF32(dir + "/" + o.value("baseline_file").toString(), Bbuf, qint64(bh) * bw)) {
        std::printf("  SKIP: baseline missing/wrong-size\n"); return -1;
    }
    if (!loadF32(dir + "/" + o.value("response_file").toString(), Rbuf, qint64(n) * bh * bw)) {
        std::printf("  SKIP: response stack missing/wrong-size\n"); return -1;
    }

    const cv::Mat B(bh, bw, CV_32F, Bbuf.data());
    TemporalBallTracker trk(rHat, fps, B, noise0);
    trk.setAddressEndIdx(addressEnd);
    const size_t stride = size_t(bh) * bw;
    for (int i = 0; i < n; ++i) {
        const cv::Mat R(bh, bw, CV_32F, Rbuf.data() + size_t(i) * stride);
        trk.push(R);
    }

    int fail = 0, checks = 0;
    auto ok = [&](bool c, const char *label) {
        std::printf("    [%s] %s\n", c ? "PASS" : "FAIL", label); ++checks; if (!c) ++fail;
    };
    auto okv = [&](bool c, const char *label, double got, double want, double tol) {
        std::printf("    [%s] %-28s got %.5f want %.5f±%.5f\n",
                    c ? "PASS" : "FAIL", label, got, want, tol);
        ++checks; if (!c) ++fail;
    };

    const auto &L = trk.locked();
    const bool refLocked = ref.contains("lock_idx");
    ok(L.valid == refLocked, "lock presence matches python");
    if (refLocked && L.valid) {
        okv(std::abs(L.x - ref.value("lock_x").toDouble()) <= 0.05, "locked x",
            L.x, ref.value("lock_x").toDouble(), 0.05);
        okv(std::abs(L.y - ref.value("lock_y").toDouble()) <= 0.05, "locked y",
            L.y, ref.value("lock_y").toDouble(), 0.05);
        ok(L.ix == ref.value("lock_ix").toInt() && L.iy == ref.value("lock_iy").toInt(),
           "integer monitor spot (ix,iy) exact");
        ok(L.idx == ref.value("lock_idx").toInt(), "lock frame exact");
        const double refMedN = ref.value("lock_medN").toDouble();
        okv(std::abs(L.medN - refMedN) <= 0.01 * std::max(1.0, std::abs(refMedN)),
            "median novelty (rel 1%)", L.medN, refMedN, 0.01 * std::max(1.0, std::abs(refMedN)));
        const double refL0 = ref.value("lock_L0").toDouble();
        okv(std::abs(L.L0 - refL0) <= 0.01 * std::max(1.0, std::abs(refL0)),
            "L0 at lock (rel 1%)", L.L0, refL0, 0.01 * std::max(1.0, std::abs(refL0)));
    }

    const auto &LA = trk.launched();
    const bool refLaunched = ref.contains("launch_idx");
    ok(LA.valid == refLaunched, "launch presence matches python");
    if (refLaunched && LA.valid)
        ok(LA.idx == ref.value("launch_idx").toInt(), "launch frame exact");
    ok(trk.falseLaunches() == ref.value("false_launches").toInt(0), "false-launch count matches");

    checksOut += checks;
    return fail;
}

int main()
{
    const QString dir = env("PP_BALL_PARITY_DIR", "/tmp/ball_parity");
    if (!QDir(dir).exists()) {
        std::printf("SKIP: fixture dir missing (%s) — generate with tools/balllab/gen_parity_ref.py\n",
                    dir.toUtf8().constData());
        return 0;
    }
    QStringList fixtures = QDir(dir).entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    if (fixtures.isEmpty()) {
        std::printf("SKIP: no *.json fixtures in %s\n", dir.toUtf8().constData());
        return 0;
    }

    std::printf("=== ball v2 C++↔python tracker parity ===\n  fixture dir: %s  (%lld fixtures)\n\n",
                dir.toUtf8().constData(), (long long)fixtures.size());

    int totalFail = 0, totalChecks = 0, ran = 0, skipped = 0;
    for (const QString &f : fixtures) {
        int checks = 0;
        const int r = runFixture(dir + "/" + f, checks);
        if (r < 0) { ++skipped; continue; }
        ++ran; totalFail += r; totalChecks += checks;
        std::printf("\n");
    }

    if (ran == 0) {
        std::printf("SKIP: all %d fixtures skipped (baselines/responses absent)\n", skipped);
        return 0;
    }
    std::printf("=== %s: %d/%d checks passed across %d fixtures (%d skipped) ===\n",
                totalFail ? "FAIL" : "PASS", totalChecks - totalFail, totalChecks, ran, skipped);
    return totalFail ? 1 : 0;
}
