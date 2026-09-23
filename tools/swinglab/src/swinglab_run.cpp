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
//                [--bind <index>=<role> ...] [--dtl [--dtl-pose p.json]]
//                [--bands <csv mm>] [--club-length-mm N] [--hosel-mm N]
//                [--shaft-length-mm N] [--hands-end-mm N]
//
// Outputs in <run_dir>:
//   result.json    swing.json-shaped document with the re-run analysis block
//   runmeta.json   provenance: source kind, wall times, params echo
//   trace.jsonl    (--trace) one line per frame: anchor + every candidate +
//                  the association choice; final line: the s_hand fit record
//   pose_dtl.json  (--dtl) the down-the-line pose pass, in the pose2d shape
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
#include <QSaveFile>
#include <QSysInfo>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <variant>
#include <vector>

#include "swing_window.h"
#include "imu_sample.h"
#include "format_descriptor.h"

#include "../../../src/Core/PpMessageLog.h"   // echo the pipeline's own log to stderr
#include "../../../src/Analysis/shot_analyzer.h"
#include "../../../src/Analysis/swing_reanalyzer.h"
#include "../../../src/Analysis/ball_runner.h"
#include "../../../src/Analysis/imu_vision_fuser.h"
#include "../../../src/Analysis/phase_segmenter.h"
#include "../../../src/Analysis/pose_runner.h"
#include "../../../src/Analysis/shaft_tracker.h"
#include "../../../src/Analysis/dtl_shaft_tracker.h"    // DTL shaft track
#include "../../../src/Analysis/dtl_face_on_witness.h"  // buildFaceOnWitness — the app's witness
#include "../../../src/Analysis/dtl_shaft_json.h"       // dtlShaftTrackToJson — club_dtl.json == analysis.clubDtl
#include "../../../src/Analysis/dtl_shaft_config.h"     // the resolved scalars echoed into club_dtl.json
#include "../../../src/Export/swing_doc.h"
#include "../../../src/Export/swing_store.h"
#include "../../../src/IMU/orientation_filter.h"     // MadgwickFilter (header-only)
#include "../../../src/IMU/orientation_refuser.h"    // refuseOrientation / refuseOrientationAdaptive / parity
#include "../../../src/Analysis/analysis_tuning.h"   // tuning::apply (filter.* keys)

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
// Build a RefuseConfig from `filter.*` tuning keys (validation §5.3.1). `fixedBeta` is the CLI
// --refuse-beta value (or the production default); `filter.beta`/`filter.betaStatic` override it.
// `filter.adaptive` switches on the phase-adaptive schedule (continuous gate + saturation + impact
// blanking); the blanking window is armed only then, anchored on the recorded impact.
RefuseConfig refuseConfigFor(const QVariantMap &ov, float fixedBeta, int64_t impactUs)
{
    namespace tn = pinpoint::analysis::tuning;
    RefuseConfig cfg;
    cfg.warmStart  = true;
    cfg.betaStatic = fixedBeta;
    tn::apply(ov, "filter.adaptive",          cfg.adaptive);
    tn::apply(ov, "filter.beta",              cfg.betaStatic);   // alias for the static gain
    tn::apply(ov, "filter.betaStatic",        cfg.betaStatic);
    tn::apply(ov, "filter.betaDynamic",       cfg.betaDynamic);
    tn::apply(ov, "filter.accelErrGateG",     cfg.accelErrGateG);
    tn::apply(ov, "filter.gyroGateDps",       cfg.gyroGateDps);
    tn::apply(ov, "filter.accelSatG",         cfg.accelSatG);
    tn::apply(ov, "filter.impactBlankPreMs",  cfg.impactBlankPreMs);
    tn::apply(ov, "filter.impactBlankPostMs", cfg.impactBlankPostMs);
    if (cfg.adaptive)
        cfg.impactUs = impactUs;   // impact-blanking window (offline-known)
    return cfg;
}

int runRefusionParity(const SwingWindow &window,
                      const std::vector<ImuSegmentBinding> &bindings,
                      const QString &outDir, double betaOverride,
                      const QVariantMap &tuning, int64_t impactUs)
{
    constexpr double kThreshDeg = 0.5;
    const float beta = betaOverride > 0.0 ? float(betaOverride) : MadgwickFilter().beta();
    const RefuseConfig baseCfg = refuseConfigFor(tuning, beta, impactUs);

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

        RefuseConfig cfg = baseCfg;
        cfg.outputRateHz = rate;
        MadgwickFilter filt(cfg.betaStatic);
        // Adaptive re-fusion intentionally DEPARTS from the live fixed-beta quat, so parity is not a
        // gate there (it is the exploration signal); the fixed-beta path keeps the strict E1 parity gate.
        const RefuseResult r = cfg.adaptive ? refuseOrientationAdaptive(filt, samples, cfg)
                                            : refuseOrientation(filt, samples, cfg);
        const ParityStats  p = parity(samples, r);
        const bool pass = cfg.adaptive ? r.warmStarted : (r.warmStarted && p.maxDeg < kThreshDeg);
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

    // The pipeline explains itself through ppInfo/ppWarn/ppError, and those go to
    // PpMessageLog — which only the GUI drains. Unset, this tool discards every
    // reason the analysis had for doing what it did, including the ONNX Runtime
    // callback in ort_log.h. That is not a preference: it is how a swing with no
    // pose track at all came back reporting "write-back ok" and a plausible score.
    // Always on, never behind a flag — a diagnostic tool that has to be asked to
    // say what went wrong will be run without the flag on the day it matters.
    PpMessageLog::instance()->setEchoToStderr(true);

    QCommandLineParser cli;
    cli.addPositionalArgument("swing_dir", "Recorded swing directory");
    QCommandLineOption optOut({ "o", "out" }, "Output run directory", "dir");
    QCommandLineOption optParams("params", "Tuning-override JSON", "file");
    QCommandLineOption optTrace("trace", "Dump per-frame shaft internals");
    QCommandLineOption optSession("session-type", "SessionController::Type", "int", "1");
    QCommandLineOption optFaceOn("face-on", "Face-on stream alias substring", "str", "Face");
    QCommandLineOption optImpact("impact-us", "Impact instant override", "us");
    QCommandLineOption optPose("pose", "Inject PoseTrack2D JSON (skip ViTPose)", "file");
    QCommandLineOption optBall("ball", "Inject BallTrack2D JSON (skip the offline ball replay)", "file");
    QCommandLineOption optRefuse("refuse-orientation",
        "Offline orientation re-fusion parity (corpus-1 gate E1): re-run Madgwick "
        "from the recorded raw accel+gyro and report disagreement vs the stored "
        "quaternion. Writes refusion.json and exits (no pose/shaft pipeline).");
    QCommandLineOption optRefuseBeta("refuse-beta",
        "Madgwick gain for re-fusion (default = production 0.05; perturb to confirm "
        "the tool detects a parameter change).", "float");
    QCommandLineOption optFullWindow("full-window", "Analyse the whole captured window (the in-app re-analyse convention) instead of the swing span");
    QCommandLineOption optForce("force-rerun", "Re-run pose/ball even when the recorded analysis versions match (analysis_versions.h)");
    QCommandLineOption optBind("bind",
        "Remap the role of recorded IMU binding <index> (0-based, in analysis.bindings[] "
        "order) to <role>: pelvis|thorax|t12|leadUpperArm|leadForearm|leadHand|trailThigh|"
        "leadThigh|club. For the kinematic-sequence truth capture "
        "(kinematic_sequence_design.md section 9): a belt and a chest Witmotion recorded in a "
        "session whose placement UX has no such slot. Repeatable.", "index=role");
    QCommandLineOption optDtl("dtl",
        "Run the down-the-line shaft tracker (dtl_shaft_tracker_design.md) beside the "
        "production analysis: pose the DTL stream, build the face-on witness from this "
        "run's own shaft track, and hand both to DtlShaftTracker. Writes pose_dtl.json, "
        "club_dtl.json and a `dtl` block in runmeta.json; result.json is untouched by the "
        "flag (it already carries the analysis's own analysis.poseDtl / clubDtl on any "
        "two-camera swing — the same tracker, see DtlShaftStage). Ignored on a swing "
        "with no DTL stream, and when --face-on names the DTL stream itself.");
    QCommandLineOption optDtlPose("dtl-pose",
        "Inject the DTL PoseTrack2D from JSON instead of running ViTPose on that stream "
        "(the pose-cache reuse --pose is for the face-on side). Pins it for BOTH consumers: "
        "the --dtl shaft block, and DtlPoseStage inside the analysis, which feeds the "
        "kinematic sequence's paired trunk route. Works without --dtl.", "file");
    // ── the club record, injected ────────────────────────────────────────────
    // A swing recorded before the app persisted capture.club carries no club
    // geometry at all, so job.bandCentersMm is empty and the E1 band matcher —
    // the one measurement that pins scale and butt offset — never runs. These
    // five say what club it was, from the tape measure, for swings the document
    // cannot answer for. Applied after the loader and before analysis, echoed
    // into runmeta.json under `clubOverride`; unset, nothing changes.
    QCommandLineOption optBands("bands",
        "Retro-band centres from the butt (mm), comma-separated, for a swing whose "
        "swing.json has no capture.club block: e.g. 308,362,560,758,808,854. Fills "
        "job.bandCentersMm so the E1 band matcher can lock.", "csv");
    QCommandLineOption optClubLen("club-length-mm",
        "Club length butt→sole (mm) for a swing with no capture.club block.", "mm");
    QCommandLineOption optHosel("hosel-mm",
        "Hosel offset from the butt (mm) for a swing with no capture.club block.", "mm");
    QCommandLineOption optShaftLen("shaft-length-mm",
        "Exposed shaft length (mm; grip end = hosel − this) for a swing with no "
        "capture.club block.", "mm");
    QCommandLineOption optHandsEnd("hands-end-mm",
        "Butt → bottom of the trail hand (mm) for a swing with no capture.club block.", "mm");
    QCommandLineOption optWriteBack("write-back",
        "Re-analyse the swing exactly as the in-app ReanalysisController does "
        "(reanalyzeSwingDir, production defaults, no overrides) and write the fresh "
        "analysis back into the SOURCE swing.json, preserving capture/streams/review. "
        "Exclusive: every other option except the positional swing dir is ignored.");
    cli.addOptions({ optOut, optParams, optTrace, optSession, optFaceOn, optImpact, optPose, optForce, optFullWindow,
                     optBall, optRefuse, optRefuseBeta, optWriteBack, optBind, optDtl, optDtlPose,
                     optBands, optClubLen, optHosel, optShaftLen, optHandsEnd });
    cli.process(app);

    if (cli.positionalArguments().isEmpty() || (!cli.isSet(optOut) && !cli.isSet(optWriteBack)))
        return fail("usage: swinglab_run <swing_dir> --out <run_dir> [--params f] [--trace]\n"
                    "                    [--bind <index>=<role> ...]\n"
                    "       swinglab_run <swing_dir> --write-back");
    const QString swingDir = cli.positionalArguments().first();

    // ── Write-back mode: the in-app re-analysis, headless ────────────────────
    // Mirrors ReanalysisController::startNext()/onWorkerFinished() exactly:
    // reanalyzeSwingDir with DEFAULT options (sessionType from the doc, span-bounded
    // like a live shot, no tuning overrides), then a manifest-preserving
    // writeSwingJson into the source dir — "analysis" is replaced, capture/streams/
    // review ride through untouched. The empty-manifest guard is the controller's:
    // never replace a document we could not read back.
    if (cli.isSet(optWriteBack)) {
        using namespace pinpoint::analysis;
        ReanalyzeOptions ropts;
        ropts.forceRerun = cli.isSet(optForce);
        ropts.fullWindow = cli.isSet(optFullWindow);
        // An EXPLICIT --session-type becomes the override (the option's default "1"
        // does not count) — the escape hatch for pre-reanalysis-era recordings that
        // never noted their session type. Everything else stays production-default.
        if (cli.isSet(optSession))
            ropts.sessionTypeOverride = cli.value(optSession).toInt();
        const ReanalyzeResult r = reanalyzeSwingDir(swingDir, ropts);
        if (!r.ok || !r.analysis.detail)
            return fail(QStringLiteral("re-analysis failed: ")
                        + (r.error.isEmpty() ? QStringLiteral("no analysis detail") : r.error));
        QString rerr;
        QJsonObject manifest = SwingStore::load(swingDir, &rerr);
        if (manifest.isEmpty())
            return fail(QStringLiteral("swing document missing/unreadable (%1) — not overwriting").arg(rerr));
        // Same as the in-app controller: the data-integrity verdict is re-reached by
        // this pass rather than inherited from capture, and is REMOVED when this pass
        // could not reach one. See swing_doc.h.
        pinpoint::applyImuIntegrity(manifest, r.imuIntegrity ? &*r.imuIntegrity : nullptr);
        QString werr;
        if (!pinpoint::SwingDocWriter::writeSwingJson(swingDir, manifest,
                                                      r.analysis.detail.get(), &werr))
            return fail(QStringLiteral("write-back failed: ") + werr);
        std::fprintf(stderr, "[swinglab] write-back ok — score %d\n", r.analysis.score);
        return 0;
    }
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

    // ── --bind: remap a recorded binding's anatomical role ────────────────────
    // The kinematic-sequence truth capture (kinematic_sequence_design.md §9) straps
    // a Witmotion to the belt and one to the sternum in a session whose placement
    // UX knows no such slot, so the document records them with whatever role the
    // wizard assigned. The remap happens HERE, before anything reads the job, so
    // every downstream stage (ImuResample, BodyRotation, KinematicSequence) sees
    // the corrected role exactly as if the wizard had known it. Nothing is
    // written back to the source swing.json.
    for (const QString &spec : cli.values(optBind)) {
        static const struct { const char *name; SegmentRole role; } kRoles[] = {
            { "pelvis",       SegmentRole::Pelvis },
            { "thorax",       SegmentRole::Thorax },
            { "t12",          SegmentRole::T12 },
            { "leadUpperArm", SegmentRole::LeadUpperArm },
            { "leadForearm",  SegmentRole::LeadForearm },
            { "leadHand",     SegmentRole::LeadHand },
            { "trailThigh",   SegmentRole::TrailThigh },
            { "leadThigh",    SegmentRole::LeadThigh },
            { "club",         SegmentRole::Club },
        };
        const int eq = spec.indexOf(QLatin1Char('='));
        bool okIdx = false;
        const int idx = eq > 0 ? spec.left(eq).trimmed().toInt(&okIdx) : -1;
        const QString roleName = eq > 0 ? spec.mid(eq + 1).trimmed() : QString();
        if (!okIdx || idx < 0 || idx >= int(job.imuBindings.size()))
            return fail(QStringLiteral("--bind %1: no recorded binding at index %2 (%3 bindings)")
                            .arg(spec).arg(idx).arg(int(job.imuBindings.size())));
        const SegmentRole *found = nullptr;
        for (const auto &r : kRoles)
            if (roleName == QLatin1String(r.name)) { found = &r.role; break; }
        if (!found)
            return fail(QStringLiteral("--bind %1: unknown role '%2' (pelvis|thorax|t12|leadUpperArm|"
                                       "leadForearm|leadHand|trailThigh|leadThigh|club)")
                            .arg(spec, roleName));
        std::fprintf(stderr, "[swinglab] bind %d: role %d -> %s\n", idx,
                     int(job.imuBindings[size_t(idx)].role), roleName.toUtf8().constData());
        job.imuBindings[size_t(idx)].role = *found;
    }

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
        return runRefusionParity(window, job.imuBindings, outDir, beta, tuning, job.impactUs);
    }

    // CLI overrides on the resolved job. Session type: explicit option wins; else
    // the recorded capture.sessionType (load()); else the option default (1).
    job.tuningOverrides = tuning;
    job.runAssessment   = true;   // SwingLab: emit Tier-2 findings into swing.json (known-groups)
    if (cli.isSet(optPose))
        job.poseTrackPath = cli.value(optPose);
    // --dtl-pose pins the DOWN-THE-LINE pose for the analyzer too, not only for the --dtl shaft
    // block below. DtlPoseStage (wrist_analyzer.cpp) runs inside analyze() on any swing with a
    // down-the-line stream, and pose inference is not deterministic — so a corpus pass over the
    // paired trunk route is only reproducible if the same cached track goes in every time. Set
    // BEFORE analyze(), and independent of --dtl: pinning the pose for the sequence does not
    // imply running the DTL shaft tracker.
    if (cli.isSet(optDtlPose))
        job.poseDtlTrackPath = cli.value(optDtlPose);
    if (cli.isSet(optBall))
        job.ballTrackPath = cli.value(optBall);
    if (cli.isSet(optSession) || job.sessionType < 0)
        job.sessionType = cli.value(optSession).toInt();
    if (cli.isSet(optImpact))
        job.impactUs = cli.value(optImpact).toLongLong();
    if (job.impactUs <= 0)
        return fail("no impact instant (no recorded Impact phase; pass --impact-us)");

    // ── the club record, injected (--bands / --club-length-mm / …) ───────────
    // The six 2026-07-04 dev swings predate capture.club, so the loader leaves
    // job.bandCentersMm empty and the E1 band matcher never gets a chance. These
    // put the tape-measured club back on the job exactly where SwingDiskLoader
    // would have, one field at a time — only the ones given, so a run with none
    // of them is bit-identical to the run before they existed. Nothing is written
    // back to the source swing.json.
    QJsonObject clubOverride;
    {
        const auto mm = [&](const QCommandLineOption &opt, const QString &name,
                            double *dst, double scale) -> QString {
            if (!cli.isSet(opt)) return {};
            bool ok = false;
            const double v = cli.value(opt).toDouble(&ok);
            if (!ok || v <= 0.0)
                return QStringLiteral("--%1 %2: not a positive number")
                           .arg(name, cli.value(opt));
            *dst = v * scale;
            clubOverride[name] = v;
            return {};
        };
        QString err = mm(optClubLen,  "club-length-mm",  &job.clubLengthM,     0.001);
        if (err.isEmpty()) err = mm(optHosel,    "hosel-mm",         &job.hoselFromButtMm, 1.0);
        if (err.isEmpty()) err = mm(optShaftLen, "shaft-length-mm",  &job.shaftLengthMm,   1.0);
        if (err.isEmpty()) err = mm(optHandsEnd, "hands-end-mm",     &job.handsEndMm,      1.0);
        if (!err.isEmpty())
            return fail(err);
        if (cli.isSet(optBands)) {
            std::vector<double> bands;
            QJsonArray echo;
            const QStringList parts = cli.value(optBands).split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (parts.isEmpty())
                return fail("--bands: no band centres in the list");
            for (const QString &p : parts) {
                bool ok = false;
                const double v = p.trimmed().toDouble(&ok);
                if (!ok || v <= 0.0)
                    return fail(QStringLiteral("--bands '%1': not a positive number (mm from the butt)")
                                    .arg(p.trimmed()));
                bands.push_back(v);
                echo.append(v);
            }
            // Ascending from the butt is the E1 matcher's contract, not a nicety —
            // an out-of-order list would lock to the wrong band and say nothing.
            if (!std::is_sorted(bands.begin(), bands.end()))
                return fail("--bands: centres must ascend from the butt");
            job.bandCentersMm = bands;
            clubOverride[QStringLiteral("bands")] = echo;
        }
        if (!clubOverride.isEmpty())
            std::fprintf(stderr, "[swinglab] club override: %s\n",
                         QJsonDocument(clubOverride).toJson(QJsonDocument::Compact).constData());
    }

    std::fprintf(stderr, "[swinglab] window rebuilt: %zu entries (%s)\n",
                 window.entries().size(), ls.usedRaw ? "raw" : "mp4");

    // Provenance for runmeta — re-read the document for the capture echo and the
    // corpus calibration verdict (independent of the reconstruction).
    const QJsonObject root = SwingStore::load(swingDir);
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
        // result.json stays JSON — it is a research output read by the Python rigs, not a library
        // document. The production writer builds it (so it is exactly what write-back would store),
        // into the run dir as swing.ppsw; it is then re-emitted as JSON and the .ppsw dropped.
        // Written with QSaveFile, so a reused --out dir gets the NEW result — the old
        // rename-onto-an-existing-file failed silently and left the previous run's result behind.
        if (!SwingDocWriter::writeSwingJson(outDir, manifest, result.detail.get(), &werr)) {
            std::fprintf(stderr, "[swinglab] result write failed: %s\n",
                         werr.toUtf8().constData());
        } else {
            QJsonObject doc = SwingStore::load(outDir, &werr);
            doc.remove(QStringLiteral("summary"));   // the library's list index; not analysis
            QSaveFile rf(outDir + "/result.json");
            if (doc.isEmpty() || !rf.open(QIODevice::WriteOnly)
                || rf.write(SwingStore::toJsonText(doc)) < 0 || !rf.commit())
                std::fprintf(stderr, "[swinglab] result.json write failed %s\n",
                             werr.toUtf8().constData());
            QFile::remove(SwingStore::ppswPath(outDir));
        }
    }

    QJsonObject meta;
    meta["swingDir"]    = swingDir;
    meta["frames"]      = ls.usedRaw ? "raw" : "mp4";
    meta["ok"]          = result.ok;
    meta["error"]       = result.error;
    meta["score"]       = result.score;
    meta["buildMs"]     = buildMs;
    meta["analyzeMs"]   = analyzeMs;
    // Per-stage analyzer wall times (plan §2 telemetry) — self-reported by the
    // analyzer, echoed next to the harness-measured analyzeMs. -1 when a stage
    // did not run (no camera / no pose frames).
    if (result.detail) {
        meta["poseMs"]  = result.detail->timings.poseMs;
        meta["ballMs"]  = result.detail->timings.ballMs;
        meta["shaftMs"] = result.detail->timings.shaftMs;
        meta["totalMs"] = result.detail->timings.totalMs;
    }
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
    // Only when something was injected — a run without the club flags writes the
    // same runmeta.json it always did.
    if (!clubOverride.isEmpty())
        meta["clubOverride"] = clubOverride;
    // Written here as always, and AGAIN at the end of a --dtl run once the `dtl`
    // block exists — so a run without --dtl produces exactly the file it did before.
    const auto writeRunmeta = [&]() {
        QFile mf(outDir + "/runmeta.json");
        if (mf.open(QIODevice::WriteOnly))
            mf.write(QJsonDocument(meta).toJson());
        else
            std::fprintf(stderr, "[swinglab] cannot write runmeta.json: %s\n",
                         mf.errorString().toUtf8().constData());
    };
    writeRunmeta();

    // ── Trace / DTL (both re-run the shaft stages with the sinks) ────────────
    // --trace and --dtl want the same thing from the face-on side: this swing's
    // decide trace. It is produced once here; --trace then writes trace.jsonl from
    // it exactly as it always did, and --dtl reads the tier/phase/chirality columns
    // into the face-on witness (dtl_shaft_tracker_design.md §5.3).
    const bool dtlAsked = cli.isSet(optDtl);
    const bool wantDtl  = dtlAsked && job.dtlSource != kInvalidSourceId;
    if (dtlAsked && !wantDtl) {
        // Name the streams that WERE there. "No down-the-line stream" on a swing
        // that plainly has two cameras is an accusation without evidence — the
        // alias and the recorded perspective are what the resolver read, so print
        // them and the reason is on the line rather than in a debugger.
        std::fprintf(stderr, "[swinglab] dtl: no down-the-line stream on this swing "
                             "(or --face-on named it) — ignoring --dtl\n");
        for (const QJsonValue &sv : root[QStringLiteral("streams")].toArray()) {
            const QJsonObject s = sv.toObject();
            if (s[QStringLiteral("kind")].toString() != QLatin1String("video"))
                continue;
            const QJsonObject setup = s[QStringLiteral("setup")].toObject();
            std::fprintf(stderr, "[swinglab] dtl:   stream alias='%s' file='%s' perspective=%s\n",
                         s[QStringLiteral("alias")].toString().toUtf8().constData(),
                         s[QStringLiteral("file")].toString().toUtf8().constData(),
                         s.contains(QStringLiteral("setup"))
                             ? QByteArray::number(setup[QStringLiteral("perspective")].toInt()).constData()
                             : "(none)");
        }
    }
    // The witness is the face-on side of the DTL pass; with no face-on camera
    // there is nothing to witness with, and the block below would not run anyway.
    if (dtlAsked && job.faceOnCameraCount == 0)
        std::fprintf(stderr, "[swinglab] dtl: no face-on camera on this swing — no witness "
                             "is possible, ignoring --dtl\n");
    if ((cli.isSet(optTrace) || wantDtl) && job.faceOnCameraCount > 0) {
        // Same data-driven grid as the live PhaseSegmenter fuse in
        // shot_processor.cpp — the default 200 here silently diverged from the
        // app the moment the live side started asking gridHzForWindow(), so a
        // trace re-run of a high-rate capture segmented differently offline
        // than it did live, which is the one thing a trace must never do.
        const FusedStreams streams = ImuVisionFuser::fuse(
            window, job.imuBindings,
            ImuVisionFuser::gridHzForWindow(window, job.imuBindings));
        const Segmentation seg = PhaseSegmenter::segment(streams, job.impactUs);
        ShotAnalysisRunnerOptions opt;
        opt.impactUs   = job.impactUs;
        opt.handedness = job.handedness;
        // The trace is the PRODUCTION run's shaft stages re-run with sinks — it
        // must see the analyzer's own pose, not a second independent one. The
        // analyzer poses two-pass/span-bounded on a camera-only swing while a
        // fresh run here is a plain full-window pass; the two grip tracks build
        // DIFFERENT hands-only phase models, and on 2026-09-10 the trace's sane
        // model masked a collapsed production one (unmarked 6-iron 0004/0005:
        // no Backswing phase, 150–160° at P2 in result.json, 4° in the trace).
        // Fresh run only when production produced no pose at all.
        const bool reuseProd = result.detail && !result.detail->pose2d.frames.empty();
        const PoseTrack2D pose = reuseProd ? result.detail->pose2d
            : job.poseTrackPath.isEmpty()
                ? PoseRunner::run(window, job.cameraSources.front(), opt)
                : PoseRunner::loadFromJson(job.poseTrackPath, job.cameraSources.front());
        // v3.4 (plan §3): same 3-way ball resolution as WristAnalyzer::analyze()
        // — explicit --ball injection wins, else whatever the swing.json/job
        // already carries, else replay the production ball detector offline.
        const BallTrack2D ball = reuseProd ? result.detail->ball
            : !job.ballTrackPath.isEmpty()
            ? BallRunner::loadFromJson(job.ballTrackPath, job.cameraSources.front())
            : (!job.ballTrack.frames.empty()
                   ? job.ballTrack
                   : BallRunner::run(window, job.cameraSources.front(), pose, opt,
                                     job.ballSearchRoi, job.ballBaseline));
        // ONE traced run, and its track is KEPT. Everything the face-on witness
        // publishes — θ, θ̇, visible length, flags, tier, phase, chirality — comes
        // from this one object pair, because a witness assembled from two runs is
        // registered against neither: the θ of run A carrying the tiers of run B
        // is wrong on exactly the frames where the runs disagree, and a count
        // comparison cannot see that.
        ShaftTracker::ShaftTrace trace;
        const ShaftTrack2D tracedFo =
            ShaftTracker::track(window, pose, ball, streams, seg, job, &trace);

        if (cli.isSet(optTrace)) {
            QFile tf(outDir + "/trace.jsonl");
            if (!tf.open(QIODevice::WriteOnly)) {
                std::fprintf(stderr, "[swinglab] cannot write trace.jsonl: %s\n",
                             tf.errorString().toUtf8().constData());
                return result.ok ? 0 : 2;
            }
            // v3.0-r1 per-emitted-frame diagnostics: DP θ, reconciled θ, ψ residual,
            // tier (0 pred/1 ray/2 band/3 recon/4 wedge), phase.
            static const char *kTierName[] = { "pred", "ray", "band", "recon", "wedge", "seg" };
            // Stage-2 head tiers (clubhead_track HeadTier): off/pred/meas.
            static const char *kHeadTierName[] = { "off", "pred", "meas" };
            for (size_t i = 0; i < trace.frameIdx.size(); ++i) {
                const int f = trace.frameIdx[i];
                const double psi = (f < int(trace.recon.psiResid.size())) ? trace.recon.psiResid[f]
                                                                          : std::numeric_limits<double>::quiet_NaN();
                QJsonObject line{
                    { "frame", f },
                    { "phase", int(trace.phases.phase[size_t(f)]) },
                    { "tier", (trace.tier[i] >= 0 && trace.tier[i] < 6) ? kTierName[trace.tier[i]] : "?" },
                    { "theta_dp", trace.dp.thetaDeg[size_t(f)] },
                    { "theta_out", trace.thetaDeg[i] },
                    { "conf", trace.conf[i] },
                    { "psi_err", std::isnan(psi) ? QJsonValue() : QJsonValue(psi) },
                    { "recon", f < int(trace.recon.recon.size()) ? bool(trace.recon.recon[size_t(f)]) : false } };
                // Additive Phase-B head columns (guarded on the vectors being filled —
                // empty unless the head pass ran). headR = temporal estimate (px),
                // headZ = raw per-frame measured radius (px).
                if (f < int(trace.headTier.size())) {
                    const int ht = trace.headTier[size_t(f)];
                    line.insert("head_tier", (ht >= 0 && ht < 3) ? kHeadTierName[ht] : "?");
                }
                if (f < int(trace.headR.size()) && std::isfinite(trace.headR[size_t(f)]))
                    line.insert("head_r", trace.headR[size_t(f)]);
                if (f < int(trace.headZ.size()) && std::isfinite(trace.headZ[size_t(f)]))
                    line.insert("head_z", trace.headZ[size_t(f)]);
                // S1 evidence-honesty calibration columns (raw pre-normalisation
                // p97 per channel + absolute ridge support at the DP θ); −1 =
                // channel/frame never ran, omitted like the head columns.
                if (f < int(trace.rawP97.size()) && trace.rawP97[size_t(f)] >= 0.0)
                    line.insert("raw_p97", trace.rawP97[size_t(f)]);
                if (f < int(trace.difP97.size()) && trace.difP97[size_t(f)] >= 0.0)
                    line.insert("dif_p97", trace.difP97[size_t(f)]);
                if (f < int(trace.supAtDp.size()) && trace.supAtDp[size_t(f)] >= 0.0)
                    line.insert("sup_dp", trace.supAtDp[size_t(f)]);
                // S2 wedge columns (empty unless shaft.wedge.enabled): the R6
                // predicted club rate, and the measured fan centroid/width where a
                // candidate was found.
                if (f < int(trace.segMode.size())) {   // E4 vs E1, same frame
                    line.insert("seg_mode", trace.segMode[size_t(f)]);
                    line.insert("seg_stage", trace.segStage[size_t(f)]);
                    if (trace.segMode[size_t(f)] > 0) {
                        line.insert("seg_pass",   trace.segPass[size_t(f)]);
                        line.insert("seg_theta",  trace.segTheta[size_t(f)]);
                        line.insert("seg_s",      trace.segS[size_t(f)]);
                        line.insert("seg_r0",     trace.segR0[size_t(f)]);
                        line.insert("seg_rg",     trace.segRG[size_t(f)]);
                        line.insert("seg_rf",     trace.segRF[size_t(f)]);
                        line.insert("seg_n",      trace.segN[size_t(f)]);
                        line.insert("seg_sup",    trace.segSup[size_t(f)]);
                        line.insert("seg_distal", trace.segDistal[size_t(f)]);
                        line.insert("seg_onset",  trace.segOnset[size_t(f)]);
                    }
                    if (trace.bandN[size_t(f)] > 0) {
                        line.insert("band_theta", trace.bandTheta[size_t(f)]);
                        line.insert("band_s",     trace.bandS[size_t(f)]);
                        line.insert("band_r0",    trace.bandR0[size_t(f)]);
                        line.insert("band_n",     trace.bandN[size_t(f)]);
                    }
                }
                if (f < int(trace.wedgeOmegaDegS.size()))
                    line.insert("omega_pred", trace.wedgeOmegaDegS[size_t(f)]);
                if (f < int(trace.wedgeCentroidDeg.size())
                    && std::isfinite(trace.wedgeCentroidDeg[size_t(f)])) {
                    line.insert("wedge_cen", trace.wedgeCentroidDeg[size_t(f)]);
                    line.insert("wedge_w",   trace.wedgeWidthDeg[size_t(f)]);
                }
                tf.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
            }
            QJsonObject summary{
                { "summary", QJsonObject{
                    { "chir", trace.chir },
                    { "seg_s_prior", trace.segSPrior },
                    { "bs0", trace.phases.bs0 }, { "top", trace.phases.top },
                    { "impact", trace.phases.impact }, { "fin0", trace.phases.fin0 },
                    { "spanLo", trace.spanLo }, { "spanHi", trace.spanHi },
                    { "heavyFrames", trace.heavyFrames },
                    // v3.4 (plan §5 gate): tk0 is compute-and-log only — never
                    // consumed by the phase model/DP above.
                    { "ballTk0Frame", trace.ballTk0Frame },
                    { "ballFrames", int(ball.frames.size()) },
                    // Stage-2 head-pass wall-clock (ms; 0 = not run). Perf gate.
                    { "headMs", trace.headMs },
                    // Length-ladder diagnostics (Phase A): which rung supplied the
                    // projected grip→head length, and the px value it chose.
                    { "projLenRung", trace.projLenRung },
                    { "projLenPx", trace.projLenPx },
                    // A1 golf-prior gate on the ball length measurement:
                    // 0 accepted/not gated, 1 ankle line, 2 feet corridor.
                    { "lPxRejected", trace.lPxRejected },
                    // S2 wedge: calibrated exposure estimate (s; −1 = wedge dark).
                    { "wedgeTExpS", trace.wedgeTExpS },
                    // P7 impact geometry (shaft.impactGeom.*): the located
                    // θ==θ_ball crossing and what the decision did (0 kept, 1
                    // override, 2 no-anchor adopt, 3 sub-frame retime). NB this
                    // trace comes from a SECOND tracker run — for anchor-derived
                    // claims read result.json's events, not `impact` above.
                    { "impactGeomTUs", double(trace.impactGeomTUs) },
                    { "impactGeomFrame", trace.impactGeomFrame },
                    { "impactGeomApplied", trace.impactGeomApplied },
                    // Top-collapse repair (shaft.topRepair.*): pre-repair top
                    // frame (-1 = dark / did not fire) and the emitted top.
                    { "topRepairApplied", trace.phases.topPreRepair >= 0 ? 1 : 0 },
                    { "topPreRepairFrame", trace.phases.topPreRepair },
                    { "topFrame", trace.phases.top } } },
                { "poseFrames", int(pose.frames.size()) },
                { "segConf", seg.conf } };
            tf.write(QJsonDocument(summary).toJson(QJsonDocument::Compact) + "\n");
            std::fprintf(stderr, "[swinglab] v3 trace: %zu emitted frames, heavy=%d chir=%d\n",
                         trace.frameIdx.size(), trace.heavyFrames, trace.chir);
        }   // --trace

        // ── --dtl: the down-the-line pass ────────────────────────────────────
        // Strictly additive. It runs AFTER result.json is written and touches
        // nothing the production analysis produced — the face-on output is a gate
        // (dtl_shaft_tracker_design.md §5.1), so --dtl on a swing must be
        // byte-identical to the same swing without it.
        //
        // That gate is why nothing here returns a failure. Every DTL trouble warns
        // and abandons the block; the process still exits on the face-on result
        // alone, because a DTL problem that turned a passing face-on run into a
        // non-zero exit would fail the parity gate for a reason parity is not
        // about.
        if (wantDtl) {
            const auto dtlSkip = [](const QString &why) {
                std::fprintf(stderr, "[swinglab] dtl: %s — skipping the DTL block\n",
                             why.toUtf8().constData());
            };
            const auto runDtl = [&] {
                QElapsedTimer dwall;
                dwall.start();

                // Scan bounds, EXPLICIT. The DTL stream has no segmentation of its
                // own and never will (§5.3: time is inherited) — so the pose pass is
                // bounded by the FACE-ON swing span, widened back a second to cover
                // the address hold (the tracker's address band lives there) and
                // forward 0.3 s past the finish. Without bounds this is a full-ring
                // ViTPose pass; with the production two-pass span-finder it would be
                // a SECOND, independent opinion about when the swing was, which is
                // the thing §5.3 forbids.
                const Segmentation &foSeg =
                    (result.detail && result.detail->segmentation.swingEndUs
                                          > result.detail->segmentation.swingStartUs)
                        ? result.detail->segmentation : seg;
                int64_t scanLo, scanHi;
                if (foSeg.swingEndUs > foSeg.swingStartUs) {
                    scanLo = foSeg.swingStartUs - 1000000;
                    scanHi = foSeg.swingEndUs   +  300000;
                } else {
                    scanLo = job.impactUs - 2500000;   // no face-on span: impact ± the design's fallback
                    scanHi = job.impactUs +  800000;
                }
                const std::vector<IndexEntry> dtlEntries = window.entriesFor(job.dtlSource);
                if (!dtlEntries.empty()) {
                    scanLo = std::max(scanLo, dtlEntries.front().timestamp_us);
                    scanHi = std::min(scanHi, dtlEntries.back().timestamp_us);
                }
                // An empty intersection is a real condition — a DTL camera that
                // started after the swing, or a clock the two streams do not share.
                // Left alone it would hand PoseRunner an inverted range, which reads
                // as "no bounds" and silently becomes the full-ring stride-1 pass
                // §5.3 forbids: hours of ViTPose producing an answer nobody asked
                // for. Say so and stop.
                if (scanHi <= scanLo) {
                    dtlSkip(QStringLiteral("the DTL stream and the face-on swing span do not "
                                           "overlap (scan window %1..%2 us is empty)")
                                .arg(qlonglong(scanLo)).arg(qlonglong(scanHi)));
                    return;
                }

                ShotAnalysisRunnerOptions dopt;
                dopt.impactUs             = job.impactUs;
                dopt.handedness           = job.handedness;
                dopt.motionCaptureQuality = job.motionCaptureQuality;
                dopt.tuningOverrides      = job.tuningOverrides;
                dopt.twoPass              = false;   // the span is inherited, not discovered
                dopt.scanStartUs          = scanLo;
                dopt.scanEndUs            = scanHi;
                // EVERY frame in the bound. The DTL bands are short and the tracker
                // solves inside them frame by frame; a stride-4 sparse zone would turn
                // a two-frame band edge into no band at all.
                dopt.denseStride          = 1;
                dopt.sparseStride         = 1;

                // The analysis already posed the DTL stream (DtlPoseStage, same span and
                // density as the bounds above) — use ITS track, so this block and the
                // app's DtlShaftStage stand on one pose. With --dtl-pose the two are the
                // same file. Re-pose only when the stage did not run.
                const bool poseInjected = cli.isSet(optDtlPose);
                const bool poseFromAnalysis = result.detail && !result.detail->poseDtl.frames.empty();
                const PoseTrack2D dtlPose =
                    poseFromAnalysis ? result.detail->poseDtl
                    : poseInjected   ? PoseRunner::loadFromJson(cli.value(optDtlPose), job.dtlSource)
                                     : PoseRunner::run(window, job.dtlSource, dopt);
                const qint64 dtlPoseMs = dwall.elapsed();
                if (poseInjected)
                    std::fprintf(stderr, "[swinglab] dtl: pose LOADED from %s\n",
                                 cli.value(optDtlPose).toUtf8().constData());
                else if (poseFromAnalysis)
                    std::fprintf(stderr, "[swinglab] dtl: pose from the analysis (DtlPoseStage)\n");

                // ── pose_dtl.json, written then READ BACK ────────────────────
                // Exactly the shape PoseRunner::fromJsonObject reads. The round-trip
                // check is not ceremony: the next package's whole cost model assumes
                // a cached DTL pose can be re-injected with --dtl-pose, and a writer
                // that silently drops the wholebody tail (or a channel's precision)
                // would make every later run a different experiment from this one.
                const QString posePath = outDir + "/pose_dtl.json";
                {
                    QJsonArray frames;
                    for (const PoseFrame2D &pf : dtlPose.frames) {
                        QJsonArray kp;
                        for (int j = 0; j < kWholeBodyJoints; ++j) {
                            kp.append(pf.kp[size_t(j)].x());
                            kp.append(pf.kp[size_t(j)].y());
                            kp.append(double(pf.conf[size_t(j)]));
                        }
                        frames.append(QJsonObject{
                            { "t_us", double(pf.t_us) },
                            { "kp", kp },
                            { "lead",  QJsonArray{ pf.leadHand.x(),  pf.leadHand.y() } },
                            { "trail", QJsonArray{ pf.trailHand.x(), pf.trailHand.y() } },
                            { "handConf", double(pf.handConf) } });
                    }
                    QFile pf(posePath);
                    if (!pf.open(QIODevice::WriteOnly)) {
                        dtlSkip(QStringLiteral("cannot write pose_dtl.json: ") + pf.errorString());
                        return;
                    }
                    pf.write(QJsonDocument(QJsonObject{ { "frames", frames } }).toJson());
                }
                {
                    const PoseTrack2D rt = PoseRunner::loadFromJson(posePath, job.dtlSource);
                    if (rt.frames.size() != dtlPose.frames.size()) {
                        dtlSkip(QStringLiteral("pose_dtl.json round-trip: %1 frames written, "
                                               "%2 read back")
                                    .arg(dtlPose.frames.size()).arg(rt.frames.size()));
                        return;
                    }
                    for (size_t i = 0; i < rt.frames.size(); ++i) {
                        const PoseFrame2D &a = dtlPose.frames[i], &b = rt.frames[i];
                        bool same = a.t_us == b.t_us && a.leadHand == b.leadHand
                                 && a.trailHand == b.trailHand && a.handConf == b.handConf;
                        for (int j = 0; same && j < kWholeBodyJoints; ++j)
                            same = a.kp[size_t(j)] == b.kp[size_t(j)]
                                && a.conf[size_t(j)] == b.conf[size_t(j)];
                        if (!same) {
                            dtlSkip(QStringLiteral("pose_dtl.json round-trip: frame %1 (t_us %2) "
                                                   "does not read back identically")
                                        .arg(i).arg(qlonglong(a.t_us)));
                            return;
                        }
                    }
                }

                // ── the face-on witness (§5.3, §5.7) ─────────────────────────
                // READ-ONLY, and one-directional: it carries face-on's schedule into
                // the DTL search and nothing comes back (§5.10).
                //
                // Every column is read off `tracedFo` and `trace` — the ONE traced
                // ShaftTracker::track run above. It used to take θ/ρ from the
                // production result and tier/phase from the trace's separate run,
                // checking only that the two had the same number of samples. Equal
                // counts do not make two runs the same run: the frame where they
                // diverge is exactly the frame where a mis-registered tier matters,
                // and the count check cannot see it.
                //
                // Built by the one builder the app's DtlShaftStage uses
                // (dtl_face_on_witness.h), from this run's traced track and ITS trace.
                if (trace.frameIdx.size() != tracedFo.samples.size()
                    || trace.tier.size() != tracedFo.samples.size())
                    std::fprintf(stderr, "[swinglab] dtl: trace/sample counts differ (%zu vs %zu) "
                                         "— witness tiers from the sample flags\n",
                                 trace.frameIdx.size(), tracedFo.samples.size());
                const FaceOnWitness wit = buildFaceOnWitness(tracedFo, &trace, job.impactUs);
                // The traced run replays the production shaft stages on the
                // production pose, so its sample TIMES must be the production ones.
                // Element-wise, not by count — same length with different times is
                // the divergence a count check was built to miss. A warning, not a
                // failure: the witness is this run's and remains self-consistent;
                // what is in doubt is whether the trace still mirrors production.
                if (result.detail) {
                    const std::vector<ShaftSample2D> &ps = result.detail->shaft.samples;
                    if (ps.size() != wit.tUs.size()) {
                        std::fprintf(stderr, "[swinglab] dtl: WARNING traced shaft track has %zu "
                                             "samples, production has %zu — the trace is no longer "
                                             "a replay of the production run\n",
                                     wit.tUs.size(), ps.size());
                    } else {
                        size_t bad = 0; size_t firstBad = 0;
                        for (size_t i = 0; i < ps.size(); ++i)
                            if (ps[i].t_us != wit.tUs[i]) { if (!bad) firstBad = i; ++bad; }
                        if (bad)
                            std::fprintf(stderr, "[swinglab] dtl: WARNING traced and production shaft "
                                                 "sample times differ at %zu/%zu samples (first at "
                                                 "index %zu: traced %lld vs production %lld) — the "
                                                 "witness is the TRACED run's\n",
                                         bad, ps.size(), firstBad,
                                         (long long)wit.tUs[firstBad], (long long)ps[firstBad].t_us);
                    }
                }

                // The DTL trace is asked for by the same --trace the face-on one
                // is, and it is the SAME RUN's internals: one solve, one post
                // pass, one set of numbers. A diagnostic that re-executes the
                // pipeline is not observing it (§3).
                DtlDecideTrace dtrace;
                const DtlShaftTrack2D dtlTrack =
                    DtlShaftTracker::track(window, dtlPose, wit.tUs.empty() ? nullptr : &wit, job,
                                           cli.isSet(optTrace) ? &dtrace : nullptr);
                std::fprintf(stderr,
                             "[swinglab] dtl: pose %zu frames, witness %zu samples, track valid=%d\n",
                             dtlPose.frames.size(), wit.tUs.size(), int(dtlTrack.valid));

                // ── club_dtl.json (schema pinpoint.clubDtl/1) ────────────────
                // Strictly additive: result.json and the exit code are untouched
                // whatever happens here. DETERMINISTIC by construction — nothing
                // time-measured goes in the file, so two runs of one swing must
                // produce byte-identical bytes, and that is a gate.
                //
                // NaN is written as JSON null, never as a NaN/inf token: those are
                // not JSON, half the readers accept them silently, and "absent"
                // has to survive the round trip as absent rather than as a number
                // whose meaning depends on the parser.
                {
                    // Same domain rule as SwingDocWriter's rel(): the analysis
                    // times go out window-relative, and an already-relative value
                    // passes through. t0 is the manifest's clock.t0_us exactly as
                    // writeSwingJson reads it, so club_dtl.json and result.json
                    // cannot end up in different time domains.
                    const qint64 t0 = qint64(manifest.value(QStringLiteral("clock")).toObject()
                                                     .value(QStringLiteral("t0_us")).toDouble());
                    const auto rel = [t0](int64_t t) -> qint64 {
                        const qint64 tt = qint64(t);
                        return tt >= t0 ? tt - t0 : tt;
                    };
                    const auto jnum = [](double v) -> QJsonValue {
                        return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
                    };

                    // The stream this track is OF, named the way the montage tool
                    // names it: matched on the recorded serial the loader keyed the
                    // source by, so the alias cannot drift from the SourceId.
                    QString dtlAlias, dtlFile;
                    dtlStreamName(root, QString::fromStdString(window.formatOf(job.dtlSource).device_serial),
                                  &dtlAlias, &dtlFile);

                    // The one builder (dtl_shaft_json.h) — the same bytes the app
                    // persists as analysis.clubDtl.
                    const DtlShaftConfig dcfg = DtlShaftConfig::fromOverrides(job.tuningOverrides);
                    const QJsonObject doc = dtlShaftTrackToJson(dtlTrack, t0, dcfg, dtlAlias, dtlFile);
                    QFile cf(outDir + "/club_dtl.json");
                    if (cf.open(QIODevice::WriteOnly))
                        cf.write(QJsonDocument(doc).toJson());
                    else
                        std::fprintf(stderr, "[swinglab] dtl: cannot write club_dtl.json: %s\n",
                                     cf.errorString().toUtf8().constData());

                    // ── trace_dtl.jsonl ──────────────────────────────────────
                    // One compact object per DTL frame, then one summary line.
                    // DETERMINISTIC: nothing time-measured goes in it, so two runs
                    // of one swing must produce byte-identical bytes, and that is a
                    // gate. It carries the SOLVED θ beside the PUBLISHED one and
                    // the snap's own numbers, because a bad frame that was
                    // mis-solved and a bad frame that was mis-registered need
                    // different fixes and a trace with one column cannot say which.
                    if (cli.isSet(optTrace)) {
                        QFile tf(outDir + "/trace_dtl.jsonl");
                        if (!tf.open(QIODevice::WriteOnly)) {
                            std::fprintf(stderr, "[swinglab] dtl: cannot write trace_dtl.jsonl: %s\n",
                                         tf.errorString().toUtf8().constData());
                        } else {
                            const auto col = [](const auto &v, int i) {
                                return i >= 0 && i < int(v.size());
                            };
                            for (int i = 0; i < int(dtlTrack.samples.size()); ++i) {
                                const DtlSample &s = dtlTrack.samples[size_t(i)];
                                QJsonObject L{
                                    { "t_us",         rel(s.t_us) },
                                    { "tier",         QString::fromLatin1(dtlTierName(s.tier)) },
                                    { "reason",       s.reason },
                                    { "theta_solved", col(dtrace.thetaSolvedDeg, i)
                                                          ? jnum(dtrace.thetaSolvedDeg[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "theta_out",    col(dtrace.thetaOutDeg, i)
                                                          ? jnum(dtrace.thetaOutDeg[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "rhoPred",      jnum(s.rhoPred) },
                                    { "rhoSrc",       QString::fromLatin1(dtlRhoSrcName(s.rhoSrc)) },
                                    { "corrCentreA",  jnum(s.corrCentreDeg[0]) },
                                    { "corrCentreB",  jnum(s.corrCentreDeg[1]) },
                                    { "corrHalf",     jnum(s.corrHalfDeg) },
                                    { "corridorOn",   s.corridorOn },
                                    { "escape",       s.corridorEscape },
                                    { "signTaken",    col(dtrace.corrSignTaken, i)
                                                          ? dtrace.corrSignTaken[size_t(i)] : 0 },
                                    { "evAtTheta",    col(dtrace.evAtTheta, i)
                                                          ? jnum(dtrace.evAtTheta[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "supAtTheta",   col(dtrace.supAtTheta, i)
                                                          ? jnum(dtrace.supAtTheta[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "rEnd",         col(dtrace.rEnd, i)
                                                          ? jnum(dtrace.rEnd[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    // The run the minimum-length rule was actually
                                    // asked about, beside rEnd rather than instead
                                    // of it: "the DP's argmax sat at its floor" and
                                    // "the club really is short here" are different
                                    // findings and one column cannot tell them apart.
                                    { "lenPx",        col(dtrace.lenPx, i)
                                                          ? jnum(dtrace.lenPx[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "lenSrc",       col(dtrace.lenSrc, i)
                                                          ? QJsonValue(QString::fromLatin1(
                                                                dtlLenSrcName(dtrace.lenSrc[size_t(i)])))
                                                          : QJsonValue(QString()) },
                                    { "lenLatOffsetPx", col(dtrace.lenLatOffsetPx, i)
                                                          ? jnum(dtrace.lenLatOffsetPx[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "ballGate",     col(dtrace.ballGate, i)
                                                          ? bool(dtrace.ballGate[size_t(i)]) : false },
                                    { "thetaBall",    col(dtrace.thetaBall, i)
                                                          ? jnum(dtrace.thetaBall[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "limbVetoJoint", col(dtrace.limbVetoJoint, i)
                                                          ? QJsonValue(QString::fromLatin1(
                                                                dtlLimbName(dtrace.limbVetoJoint[size_t(i)])))
                                                          : QJsonValue(QString()) },
                                    { "d3LenCost",    col(dtrace.d3LenCost, i)
                                                          ? jnum(dtrace.d3LenCost[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "rowResid",     col(dtrace.rowResid, i)
                                                          ? jnum(dtrace.rowResid[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "quarantined",  col(dtrace.quarantine, i)
                                                          ? bool(dtrace.quarantine[size_t(i)]) : false },
                                    { "snapOffsetPx", col(dtrace.snapOffsetPx, i)
                                                          ? jnum(dtrace.snapOffsetPx[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "snapDThetaDeg", col(dtrace.snapDThetaDeg, i)
                                                          ? jnum(dtrace.snapDThetaDeg[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "snapAccepted", col(dtrace.snapAccepted, i)
                                                          ? bool(dtrace.snapAccepted[size_t(i)]) : false },
                                    // The snap's own support numbers and the
                                    // extent its objective was averaged over.
                                    // cfg.lineConfRay was set from the first of
                                    // them, and the address direction error was
                                    // the third, so a trace without them cannot
                                    // be used to argue about either.
                                    { "snapBestConf", col(dtrace.snapBestConf, i)
                                                          ? jnum(dtrace.snapBestConf[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "snapOriginConf", col(dtrace.snapOriginConf, i)
                                                          ? jnum(dtrace.snapOriginConf[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    { "snapDrawnPx",  col(dtrace.snapDrawnPx, i)
                                                          ? jnum(dtrace.snapDrawnPx[size_t(i)])
                                                          : QJsonValue(QJsonValue::Null) },
                                    // Which gate published the frame, and — where
                                    // the reverse-ray test was not held against it
                                    // — why. "the test passed" and "the test was
                                    // waived" are different statements.
                                    { "evSrc",        col(dtrace.evSrc, i)
                                                          ? QJsonValue(QString::fromLatin1(
                                                                dtlEvSrcName(dtrace.evSrc[size_t(i)])))
                                                          : QJsonValue(QString()) },
                                    { "revWaived",    col(dtrace.revWaived, i)
                                                          ? QJsonValue(QString::fromLatin1(
                                                                dtlRevWaiverName(dtrace.revWaived[size_t(i)])))
                                                          : QJsonValue(QString()) },
                                    { "band",         s.band } };
                                tf.write(QJsonDocument(L).toJson(QJsonDocument::Compact));
                                tf.write("\n");
                            }
                            QJsonArray tbands;
                            for (const DtlBand &d : dtlTrack.bands)
                                tbands.append(QJsonObject{ { "lo_us", rel(d.loUs) },
                                                           { "hi_us", rel(d.hiUs) },
                                                           { "name",  d.name } });
                            const QJsonObject sum{
                                { "summary", QJsonObject{
                                    { "bands",   tbands },
                                    { "ball",    QJsonObject{ { "found",  dtlTrack.ball.found },
                                                              { "x",      jnum(dtlTrack.ball.x) },
                                                              { "y",      jnum(dtlTrack.ball.y) },
                                                              { "reason", dtlTrack.ball.reason },
                                                              { "source", dtlTrack.ball.source },
                                                              { "shadowX",     jnum(dtlTrack.ball.shadowX) },
                                                              { "shadowY",     jnum(dtlTrack.ball.shadowY) },
                                                              { "launchRise",  jnum(dtlTrack.ball.launchRise) },
                                                              { "radiusPx",    jnum(dtlTrack.ball.radiusPx) },
                                                              { "nCandidates", dtlTrack.ball.nCandidates } } },
                                    { "lFullPx",     jnum(dtlTrack.lFullPx) },
                                    { "lFullSource", dtlTrack.lFullSource },
                                    { "clubAwayWindow", dtlTrack.clubAwayWindow },
                                    { "rowFitA",     jnum(dtrace.rowFitA) },
                                    { "rowFitB",     jnum(dtrace.rowFitB) },
                                    { "configHash",  dtlConfigHash(dcfg) } } } };
                            tf.write(QJsonDocument(sum).toJson(QJsonDocument::Compact));
                            tf.write("\n");
                        }
                    }
                }

                meta["dtl"] = QJsonObject{
                    { "poseFrames", int(dtlPose.frames.size()) },
                    { "poseMs",     dtlPoseMs },
                    { "source",     int(job.dtlSource) },
                    { "posePath",   posePath } };
                writeRunmeta();
            };
            runDtl();
        }
    }

    return result.ok ? 0 : 2;
}
