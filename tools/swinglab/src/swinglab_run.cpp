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

// swinglab_run — the SwingLab offline runner (swinglab_impl.md, stage L1).
//
// Reconstructs a SwingWindow from a recorded PinPointStudio swing dir and executes
// the unmodified production analysis pipeline (makeShotAnalyzer). The disk →
// SwingWindow → analyzer reconstruction is shared with the in-app re-analyse path
// via SwingDiskLoader (src/Analysis/swing_reanalyzer) — it STREAMS frames from disk
// rather than rebuilding a full EventBuffer in RAM. Tuning params are injectable
// from JSON; --trace re-runs the shaft stages with the trace sinks.
//
//   swinglab_run <swing_dir> --out <run_dir> [--params p.json] [--trace]
//                [--session-type 1] [--face-on Face] [--impact-us N] [--pose p.json]
//
// Outputs in <run_dir>:
//   result.json    swing.json-shaped document with the re-run analysis block
//   runmeta.json   provenance: source kind, wall times, params echo
//   trace.jsonl    (--trace) one line per frame: anchor + every candidate +
//                  the association choice; final line: the s_hand fit record
//
// Frames come from the raw sidecars (bit-faithful) when present, else the MP4s
// (decoded to BGR24) — both streamed one frame at a time by SwingDiskSource.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QVariantMap>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <variant>
#include <vector>

#include "swing_window.h"
#include "imu_sample.h"
#include "format_descriptor.h"

#include "../../../src/Analysis/shot_analyzer.h"
#include "../../../src/Analysis/swing_reanalyzer.h"
#include "../../../src/Analysis/imu_vision_fuser.h"
#include "../../../src/Analysis/phase_segmenter.h"
#include "../../../src/Analysis/pose_runner.h"
#include "../../../src/Analysis/shaft_tracker.h"
#include "../../../src/Export/swing_doc.h"
#include "../../../src/IMU/orientation_filter.h"     // MadgwickFilter (header-only)
#include "../../../src/IMU/orientation_refuser.h"    // refuseOrientation / parity

using namespace pinpoint;
using namespace pinpoint::analysis;

namespace {

// Flatten {"shaft": {"ridgeKernelPx": 11}} and/or {"shaft.ridgeKernelPx": 11}
// into the dotted-key map the tuning hooks consume.
QVariantMap flattenParams(const QJsonObject &o, const QString &prefix = {})
{
    QVariantMap out;
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString key = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
        if (it->isObject())
            out.insert(flattenParams(it->toObject(), key));
        else
            out.insert(key, it->toVariant());
    }
    return out;
}

int fail(const QString &msg)
{
    std::fprintf(stderr, "swinglab_run: %s\n", msg.toUtf8().constData());
    return 1;
}

// Offline orientation RE-FUSION parity — the corpus-1 pre-collection gate E1
// (docs/validation/pipeline_validation_and_tuning.md §5.2, §5.3.1). For each
// bound IMU, re-runs the MADGWICK filter from the recorded raw accel+gyro,
// warm-started from the stored quaternion at the window's first sample
// (RefuseSample carries the same imu_sample_v2 fields the live filter saw), and
// reports the per-binding geodesic disagreement vs the stored live quaternion.
// Writes refusion.json + a stderr summary; returns 0 on PASS.
//
// Madgwick only: it is the ship default and the only filter that warm-starts
// EXACTLY (ESKF's vendored reference quaternion is not settable). The parity gate
// therefore assumes the swing was captured with the Madgwick default; a wholesale
// disagreement means an ESKF capture OR a schema gap — which is exactly what E1
// must catch before bulk capture. --refuse-beta perturbs the gain on purpose, to
// confirm the tool can see a parameter change (parity should then diverge).
int runRefusionParity(const SwingWindow &window,
                      const std::vector<ImuSegmentBinding> &bindings,
                      const QString &outDir, double betaOverride)
{
    constexpr double kThreshDeg = 0.5;
    const float beta = betaOverride > 0.0 ? float(betaOverride) : MadgwickFilter().beta();

    QJsonArray perSource;
    int nChecked = 0, nPass = 0;
    double worst = 0.0;

    // Re-fusion is binding-INDEPENDENT — it needs each IMU's raw samples, not the
    // A/M. Enumerate IMU sources straight from the window so a swing with
    // bindings=0 (the existing-recordings failure mode) is still parity-checkable;
    // the role is looked up from bindings only as a label when present.
    std::vector<SourceId> imuIds;
    for (const IndexEntry &e : window.entries())
        if (std::holds_alternative<ImuFormat>(window.formatOf(e.source_id).format)
            && std::find(imuIds.begin(), imuIds.end(), e.source_id) == imuIds.end())
            imuIds.push_back(e.source_id);
    const auto roleOf = [&](SourceId id) -> int {
        for (const ImuSegmentBinding &b : bindings)
            if (b.source == id) return int(b.role);
        return -1;   // unbound source (re-fusion still valid)
    };

    for (const SourceId sid : imuIds) {
        const std::vector<IndexEntry> entries = window.entriesFor(sid);
        if (entries.size() < 2)
            continue;

        // Nominal cadence from the IMU format descriptor -> dt = 1/rate, matching
        // ImuBase::fuseRawImu (burst-immune; NOT per-sample timestamp deltas).
        float rate = 200.0f;
        const FormatDescriptor &fd = window.formatOf(sid);
        const QString serial = QString::fromStdString(fd.device_serial);
        if (const auto *imf = std::get_if<ImuFormat>(&fd.format)) {
            if (imf->sample_rate_hz > 0)
                rate = float(imf->sample_rate_hz);
        }

        std::vector<RefuseSample> samples;
        samples.reserve(entries.size());
        for (const IndexEntry &e : entries) {
            const SourceRing::ReadHandle h = window.payloadOf(e);
            if (!h.data || h.bytes < sizeof(ImuSample))
                continue;
            ImuSample s;
            std::memcpy(&s, h.data, sizeof(ImuSample));   // alignment-safe
            samples.push_back(RefuseSample{ e.timestamp_us,
                                            s.accel_x, s.accel_y, s.accel_z,
                                            s.gyro_x,  s.gyro_y,  s.gyro_z,
                                            s.quat_w,  s.quat_x,  s.quat_y, s.quat_z });
        }
        if (samples.size() < 2)
            continue;

        RefuseConfig cfg;
        cfg.outputRateHz = rate;
        cfg.warmStart    = true;
        MadgwickFilter filt(beta);
        const RefuseResult r = refuseOrientation(filt, samples, cfg);
        const ParityStats  p = parity(samples, r);
        const bool pass = r.warmStarted && p.maxDeg < kThreshDeg;
        ++nChecked;
        if (pass) ++nPass;
        worst = std::max(worst, p.maxDeg);

        perSource.append(QJsonObject{
            { "serial", serial }, { "role", roleOf(sid) },
            { "samples", int(samples.size()) }, { "rateHz", rate },
            { "warmStarted", r.warmStarted },
            { "meanDeg", p.meanDeg }, { "rmsDeg", p.rmsDeg },
            { "maxDeg", p.maxDeg }, { "p95Deg", p.p95Deg },
            { "maxAtSample", int(p.maxAt) }, { "pass", pass } });

        std::fprintf(stderr,
            "[refuse] %-12s role=%d n=%zu rate=%.0fHz warm=%d  max=%.5f mean=%.5f p95=%.5f deg  %s\n",
            serial.isEmpty() ? "(imu)" : serial.toUtf8().constData(),
            roleOf(sid), samples.size(), double(rate), int(r.warmStarted),
            p.maxDeg, p.meanDeg, p.p95Deg, pass ? "PASS" : "FAIL");
    }

    const bool ok = nChecked > 0 && nPass == nChecked;
    const QJsonObject doc{
        { "schema", "pinpoint.refusion/1" }, { "filter", "madgwick" }, { "beta", double(beta) },
        { "thresholdDeg", kThreshDeg },
        { "sourcesChecked", nChecked }, { "sourcesPassed", nPass },
        { "worstMaxDeg", worst }, { "pass", ok }, { "sources", perSource },
        { "note", "Warm-start parity reproduces the live Madgwick fusion exactly when the "
                  "swing was captured with the Madgwick default filter; a large disagreement "
                  "means an ESKF capture or a schema gap (the corpus-1 E1 gate catches the "
                  "latter before bulk capture)." } };
    QFile of(outDir + "/refusion.json");
    if (of.open(QIODevice::WriteOnly))
        of.write(QJsonDocument(doc).toJson());
    else
        std::fprintf(stderr, "[refuse] cannot write refusion.json: %s\n",
                     of.errorString().toUtf8().constData());

    if (nChecked == 0) {
        std::fprintf(stderr, "[refuse] no IMU sources in the window to re-fuse (check capture)\n");
        return 1;
    }
    std::fprintf(stderr, "[refuse] %d/%d IMU sources pass (worst max %.5f deg, threshold %.2f) -> %s\n",
                 nPass, nChecked, worst, kThreshDeg, ok ? "PASS" : "FAIL");
    return ok ? 0 : 3;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser cli;
    cli.addPositionalArgument("swing_dir", "Recorded swing directory");
    QCommandLineOption optOut({ "o", "out" }, "Output run directory", "dir");
    QCommandLineOption optParams("params", "Tuning-override JSON", "file");
    QCommandLineOption optTrace("trace", "Dump per-frame shaft internals");
    QCommandLineOption optSession("session-type", "SessionController::Type", "int", "1");
    QCommandLineOption optFaceOn("face-on", "Face-on stream alias substring", "str", "Face");
    QCommandLineOption optImpact("impact-us", "Impact instant override", "us");
    QCommandLineOption optPose("pose", "Inject PoseTrack2D JSON (skip ViTPose)", "file");
    QCommandLineOption optRefuse("refuse-orientation",
        "Offline orientation re-fusion parity (corpus-1 gate E1): re-run Madgwick "
        "from the recorded raw accel+gyro and report disagreement vs the stored "
        "quaternion. Writes refusion.json and exits (no pose/shaft pipeline).");
    QCommandLineOption optRefuseBeta("refuse-beta",
        "Madgwick gain for re-fusion (default = production 0.05; perturb to confirm "
        "the tool detects a parameter change).", "float");
    cli.addOptions({ optOut, optParams, optTrace, optSession, optFaceOn, optImpact, optPose,
                     optRefuse, optRefuseBeta });
    cli.process(app);

    if (cli.positionalArguments().isEmpty() || !cli.isSet(optOut))
        return fail("usage: swinglab_run <swing_dir> --out <run_dir> [--params f] [--trace]");
    const QString swingDir = cli.positionalArguments().first();
    const QString outDir   = cli.value(optOut);
    QDir().mkpath(outDir);

    QElapsedTimer wall;
    wall.start();

    QVariantMap tuning;
    if (cli.isSet(optParams)) {
        QFile pf(cli.value(optParams));
        if (!pf.open(QIODevice::ReadOnly))
            return fail("cannot open params file");
        tuning = flattenParams(QJsonDocument::fromJson(pf.readAll()).object());
    }

    // ── Reconstruct (streaming) + resolve the job from swing.json ─────────────
    SwingLoadOptions lopts;
    lopts.faceOnExplicit  = cli.isSet(optFaceOn);
    lopts.faceOnSubstring = cli.value(optFaceOn);
    LoadedSwing ls = SwingDiskLoader::load(swingDir, lopts);
    if (!ls.ok)
        return fail(ls.error);
    SwingWindow     &window = *ls.window;
    ShotAnalysisJob &job    = ls.job;

    // ── Orientation re-fusion parity (corpus-1 gate E1) ──────────────────────
    // Independent of impact / pose / shaft: re-fuse the IMU offline and compare to
    // the stored quaternion, then exit. Run this on pilot swings BEFORE bulk
    // capture to prove the corpus is post-hoc-tunable (the raw data is persisted
    // but the live filter must reproduce from it). bindings=0 here is the same
    // fatal capture gap the Tier-0 gate guards against.
    if (cli.isSet(optRefuse)) {
        std::fprintf(stderr, "[swinglab] window rebuilt: %zu entries (%s)\n",
                     window.entries().size(), ls.usedRaw ? "raw" : "mp4");
        const double beta = cli.isSet(optRefuseBeta) ? cli.value(optRefuseBeta).toDouble() : -1.0;
        return runRefusionParity(window, job.imuBindings, outDir, beta);
    }

    // CLI overrides on the resolved job. Session type: explicit option wins; else
    // the recorded capture.sessionType (load()); else the option default (1).
    job.tuningOverrides = tuning;
    if (cli.isSet(optPose))
        job.poseTrackPath = cli.value(optPose);
    if (cli.isSet(optSession) || job.sessionType < 0)
        job.sessionType = cli.value(optSession).toInt();
    if (cli.isSet(optImpact))
        job.impactUs = cli.value(optImpact).toLongLong();
    if (job.impactUs <= 0)
        return fail("no impact instant (no recorded Impact phase; pass --impact-us)");

    std::fprintf(stderr, "[swinglab] window rebuilt: %zu entries (%s)\n",
                 window.entries().size(), ls.usedRaw ? "raw" : "mp4");

    // Provenance for runmeta — re-read swing.json for the capture echo and the
    // corpus calibration verdict (independent of the reconstruction).
    QJsonObject root;
    {
        QFile rf(swingDir + "/swing.json");
        if (rf.open(QIODevice::ReadOnly))
            root = QJsonDocument::fromJson(rf.readAll()).object();
    }
    const QJsonObject captureIn = root["capture"].toObject();
    int calibKnown = 0, calibTrue = 0;
    for (const QJsonValue &bv : root["analysis"].toObject()["bindings"].toArray()) {
        const QJsonObject b = bv.toObject();
        if (b.contains("calibrated")) {
            ++calibKnown;
            if (b["calibrated"].toBool()) ++calibTrue;
        }
    }

    // ── Run the production pipeline ──────────────────────────────────────────
    const qint64 buildMs = wall.elapsed();
    auto analyzer = makeShotAnalyzer(job.sessionType);
    const ShotAnalysisResult result = analyzer->analyze(window, job);
    const qint64 analyzeMs = wall.elapsed() - buildMs;
    std::fprintf(stderr, "[swinglab] analysis %s in %lld ms (score %d)%s%s\n",
                 result.ok ? "ok" : "FAILED", (long long)analyzeMs, result.score,
                 result.ok ? "" : ": ", result.ok ? "" : result.error.toUtf8().constData());

    // ── Outputs ──────────────────────────────────────────────────────────────
    QJsonObject manifest;
    manifest["schema"]  = "pinpoint.swinglab/1";
    manifest["source"]  = QJsonObject{ { "swingDir", swingDir },
                                       { "frames", ls.usedRaw ? "raw" : "mp4" } };
    QString werr;
    if (result.detail) {
        if (!SwingDocWriter::writeSwingJson(outDir, manifest, result.detail.get(), &werr))
            std::fprintf(stderr, "[swinglab] result write failed: %s\n",
                         werr.toUtf8().constData());
        QFile::rename(outDir + "/swing.json", outDir + "/result.json");
    }

    QJsonObject meta;
    meta["swingDir"]    = swingDir;
    meta["frames"]      = ls.usedRaw ? "raw" : "mp4";
    meta["ok"]          = result.ok;
    meta["error"]       = result.error;
    meta["score"]       = result.score;
    meta["buildMs"]     = buildMs;
    meta["analyzeMs"]   = analyzeMs;
    meta["params"]      = QJsonObject::fromVariantMap(tuning);
    meta["impactUs"]    = job.impactUs;
    meta["bindings"]    = int(job.imuBindings.size());
    meta["host"]        = QSysInfo::machineHostName();
    meta["platform"]    = QSysInfo::prettyProductName();
    meta["sessionType"] = job.sessionType;
    // Verbatim capture echo (empty object for legacy swings) + the corpus
    // calibration verdict: true/false when recorded, null when unknown.
    meta["capture"]    = captureIn;
    meta["calibrated"] = calibKnown == 0 ? QJsonValue(QJsonValue::Null)
                                         : QJsonValue(calibTrue == calibKnown);
    {
        QFile mf(outDir + "/runmeta.json");
        if (mf.open(QIODevice::WriteOnly))
            mf.write(QJsonDocument(meta).toJson());
        else
            std::fprintf(stderr, "[swinglab] cannot write runmeta.json: %s\n",
                         mf.errorString().toUtf8().constData());
    }

    // ── Trace (re-runs the shaft stages with the sinks) ──────────────────────
    if (cli.isSet(optTrace) && job.faceOnCameraCount > 0) {
        const FusedStreams streams = ImuVisionFuser::fuse(window, job.imuBindings);
        const Segmentation seg = PhaseSegmenter::segment(streams, job.impactUs);
        ShotAnalysisRunnerOptions opt;
        opt.impactUs   = job.impactUs;
        opt.handedness = job.handedness;
        const PoseTrack2D pose = job.poseTrackPath.isEmpty()
            ? PoseRunner::run(window, job.cameraSources.front(), opt)
            : PoseRunner::loadFromJson(job.poseTrackPath, job.cameraSources.front());
        ShaftTracker::ShaftTrace trace;
        ShaftTracker::track(window, pose, streams, seg, job, &trace);

        QFile tf(outDir + "/trace.jsonl");
        if (!tf.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "[swinglab] cannot write trace.jsonl: %s\n",
                         tf.errorString().toUtf8().constData());
            return result.ok ? 0 : 2;
        }
        for (size_t i = 0; i < trace.obs.size(); ++i) {
            const ShaftFrameObs &o = trace.obs[i];
            QJsonArray cands;
            for (const ShaftCandidate &c : o.candidates)
                cands.append(QJsonObject{
                    { "theta", c.thetaRad }, { "sigma", c.sigmaThetaRad },
                    { "len", c.visibleLenPx }, { "score", c.score },
                    { "wedge", c.wedge },
                    { "head", QJsonArray{ c.headPx.x, c.headPx.y } } });
            QJsonObject line{
                { "t_us", qint64(o.t_us) },
                { "grip", QJsonArray{ o.gripPx.x, o.gripPx.y } },
                { "qHandValid", o.qHandValid },
                { "candidates", cands },
                { "selected", i < trace.assembly.selected.size()
                                  ? trace.assembly.selected[i] : -1 } };
            tf.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
        }
        const auto &fit = trace.assembly.fit;
        QJsonObject fitLine{
            { "fit", QJsonObject{
                { "ok", fit.ok }, { "sign", fit.sign },
                { "offsetRad", fit.offsetRad }, { "residualRad", fit.residualRad },
                { "framesUsed", fit.framesUsed },
                { "sHand", QJsonArray{ fit.sHand[0], fit.sHand[1], fit.sHand[2] } } } },
            { "poseFrames", int(pose.frames.size()) },
            { "segConf", seg.conf } };
        tf.write(QJsonDocument(fitLine).toJson(QJsonDocument::Compact) + "\n");
        std::fprintf(stderr, "[swinglab] trace: %zu frames, fit.ok=%d residual=%.4f\n",
                     trace.obs.size(), fit.ok, fit.residualRad);
    }

    return result.ok ? 0 : 2;
}
