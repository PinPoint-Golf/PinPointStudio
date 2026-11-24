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

// swing_doc_writer.cpp — writing a swing document: SwingDocWriter (writeDocument,
// updateStreamOrigin, editReview, updateLaunchMonitor, refreshLaunchMonitorRows,
// writeDeviceOnlySwing, convertToPpsw, takeJustWritten; private since step 1.4, reached through
// RecordingRepository — swing_store_repository.cpp), the refusal of a read-only document every
// write makes (refusesWrite), the one
// write every writer ends in (saveDocument), the launch monitor blocks and their derived rows,
// and the two integrity blocks (imuIntegrityJson / applyImuIntegrity, captureIntegrityJson /
// applyCaptureIntegrity). The `analysis` block itself is built in swing_doc_analysis.cpp and the
// `summary` block by swing_doc_reader.cpp's summary builders (swing_doc_internal.h); the public
// API is swing_doc.h. Split out of swing_doc.cpp verbatim (refactor step 1.0, 14 Nov 2026).

#include "swing_doc.h"
#include "swing_doc_internal.h"
#include "recording_header.h"   // the one header writer; the one schema constant; readAnalysisContext / clubReviewOf

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

#include "swing_store.h"
#include "../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S): a rewrite's read and its save
#include "../IMU/imu_refusion_check.h"
#include "../Analysis/capture_integrity_check.h"
#include "../Analysis/manifest_gates.h"            // the integrity blocks → analysis.gates (shot.*)
#include "../LaunchMonitor/lm_inferred_reads.h"
#include "../LaunchMonitor/lm_gates.h"              // the import's dropped fields → analysis.gates
#include "../LaunchMonitor/lm_device.h"             // connector vs device: launchMonitor.device
#include "../Vocabulary/Analysis/swing_analysis.h"
#include "../Metrics/measurement.h"            // lmDerivedKeys: the vocabulary's one list (6.1a)
#include "../Vocabulary/Analysis/uncertainty_json.h"         // σ fields — one spelling, four paths
#include "../Club/dtl_shaft_json.h"                // dtlStreamName — the DTL club block names its stream
#include "../Core/club_standards.h"            // lmArcRadiusM — the club's standard length
#include "../Analysis/effective_context.h"     // lmArcRadiusM — the effective club (step 1.6)
#include "../Core/club_vocabulary.h"

namespace pinpoint {

// The helpers more than one swing_doc_*.cpp calls (swing_doc_internal.h).
using swingdoc_detail::serializeAnalysis;
using swingdoc_detail::summaryFromRoot;
using swingdoc_detail::summaryJson;

QJsonObject imuIntegrityJson(const ImuRefusionVerdict &v)
{
    return QJsonObject{
        { QStringLiteral("refusionOk"),     v.ok },
        { QStringLiteral("worstMaxDeg"),    v.worstMaxDeg },
        { QStringLiteral("sourcesChecked"), v.sourcesChecked },
        { QStringLiteral("thresholdDeg"),   v.thresholdDeg },
        // ⚠ "madgwick" describes the sources that WERE checked, not every IMU lane
        // in the window. checkImuRefusion skips HackMotion lanes per-source (they
        // stream their own orientation and cannot be re-fused), so a mixed window
        // reports on the checkable half only. `sourcesChecked` is the field that
        // says how many that was, and the block is omitted when it is zero.
        { QStringLiteral("filter"),         QStringLiteral("madgwick") } };
}

void applyImuIntegrity(QJsonObject &manifest, const ImuRefusionVerdict *v)
{
    // sourcesChecked == 0 is "nothing checkable was present" — a wG3-only window,
    // say — which is a no-claim outcome exactly as nullptr is, not a pass.
    if (v && v->sourcesChecked > 0)
        manifest[QStringLiteral("imuIntegrity")] = imuIntegrityJson(*v);
    else
        manifest.remove(QStringLiteral("imuIntegrity"));
}

QJsonObject captureIntegrityJson(const CaptureIntegrityVerdict &v)
{
    QJsonObject o{
        { QStringLiteral("ok"),             v.ok },
        { QStringLiteral("camerasChecked"), v.camerasChecked },
        { QStringLiteral("holes"),          v.holes },
        { QStringLiteral("framesLost"),     v.framesLost },
        { QStringLiteral("worstHoleMs"),    v.worstHoleMs },
        { QStringLiteral("firstHoleUs"),    v.firstHoleUs },
        { QStringLiteral("preImpact"),      v.preImpact },
        { QStringLiteral("postImpact"),     v.postImpact },
        { QStringLiteral("holePeriods"),    v.holePeriods } };
    // The first hole in ms from impact (+ after): only when there is a hole and impact was known.
    // Additive (trust stage 6) — the shot.captureHoles record quotes it (manifest_gates.h).
    if (std::isfinite(v.firstHoleMs))
        o.insert(QStringLiteral("firstHoleMs"), v.firstHoleMs);
    // Coverage (issue #53): how far each camera's video falls short of the capture window, and how
    // many of its frames came before impact. Additive — a block written before it has none of these
    // keys, and every reader takes their absence as "not judged". Unknowns are left out, never 0.
    if (v.coverageJudged) {
        const auto putIfKnown = [](QJsonObject &obj, const char *key, double ms) {
            if (std::isfinite(ms)) obj.insert(QLatin1String(key), ms);
        };
        o.insert(QStringLiteral("edgeToleranceMs"),   v.edgeToleranceMs);
        o.insert(QStringLiteral("lateStart"),         v.lateStart);
        o.insert(QStringLiteral("earlyEnd"),          v.earlyEnd);
        o.insert(QStringLiteral("startsAfterImpact"), v.startsAfterImpact);
        putIfKnown(o, "windowMs",       v.windowMs);
        putIfKnown(o, "missingStartMs", v.missingStartMs);
        putIfKnown(o, "missingEndMs",   v.missingEndMs);
        if (v.framesBeforeImpact >= 0)
            o.insert(QStringLiteral("framesBeforeImpact"), v.framesBeforeImpact);
        QJsonArray cams;
        for (const CaptureLaneCoverage &c : v.cameras) {
            QJsonObject co;
            if (!c.serial.empty()) co.insert(QStringLiteral("serial"), QString::fromStdString(c.serial));
            putIfKnown(co, "firstFrameMs",   c.firstFrameMs);
            putIfKnown(co, "lastFrameMs",    c.lastFrameMs);
            putIfKnown(co, "missingStartMs", c.missingStartMs);
            putIfKnown(co, "missingEndMs",   c.missingEndMs);
            if (c.framesBeforeImpact >= 0)
                co.insert(QStringLiteral("framesBeforeImpact"), c.framesBeforeImpact);
            cams.append(co);
        }
        o.insert(QStringLiteral("cameras"), cams);
    }
    return o;
}

void applyCaptureIntegrity(QJsonObject &manifest, const CaptureIntegrityVerdict *v)
{
    // camerasChecked == 0 (no camera lane, or none with enough frames to judge) is a
    // no-claim outcome exactly as nullptr is — never persisted as "checked and passed".
    if (v && v->camerasChecked > 0)
        manifest[QStringLiteral("captureIntegrity")] = captureIntegrityJson(*v);
    else
        manifest.remove(QStringLiteral("captureIntegrity"));
}

namespace {

// ── Launch monitor blocks, shared by the two paths that write them ──────────
//
// updateLaunchMonitor() folds a reading into a swing the capture pipeline produced;
// writeDeviceOnlySwing() creates the whole document from the reading alone. They must
// emit byte-identical blocks or a device-only shot and a camera shot would carry the
// same measurement in two shapes, which is exactly the drift a reader would never spot.

QJsonObject lmRawBlock(const lm::LaunchMonitorReading &reading)
{
    // Everything, including the columns no metric consumes. The reading is already in
    // catalogue units, so this and the metric entries cannot disagree — storing the
    // file's own units here as well would invite exactly that.
    QJsonObject raw{
        { QStringLiteral("kind"),          reading.deviceKind },
        { QStringLiteral("deviceShotId"),  reading.deviceShotId },
        { QStringLiteral("deviceClub"),    reading.deviceClub },
        { QStringLiteral("sourcePath"),    reading.sourcePath },
        { QStringLiteral("readAtMs"),      reading.readAtMs },
    };
    // The device's own contact estimate (Foresight Premier only): additive, absent for every
    // other connector and every swing written before it existed.
    if (reading.contactAtMs)
        raw.insert(QStringLiteral("contactAtMs"), double(*reading.contactAtMs));
    // WHICH DEVICE, apart from which connector (lm_device.h): what the client said it is, when
    // the table recognises it, else what the connector always reads. Absent when neither says —
    // the reader then keys it by its connector ("some device, via GSPro"). The client's own words
    // are never written (LaunchMonitorReading::reportedDevice).
    if (const QString device = lm::resolveDeviceKey(reading.deviceKind, reading.reportedDevice);
        !device.isEmpty())
        raw.insert(QStringLiteral("device"), device);
    for (const lm::FieldDef &f : lm::fieldDefs())
        if (const auto &v = reading.*(f.member))
            raw.insert(QString::fromLatin1(f.rawName), *v);
    // What the import dropped and why (trust stage 6; lm_gates.h turns it into analysis.gates on
    // every write). Written even when empty, so "nothing was dropped" reads apart from a block
    // imported before the list was kept (no key: those swings stay unexplained).
    raw.insert(QStringLiteral("dropped"), lm::droppedFieldsToJson(reading.dropped));
    return raw;
}

// One entry per reading: an EMPTY CURVE carrying a single phaseSample at Impact — the
// representation club_delivery already uses for its scalars. A launch monitor reports
// one number per shot; inventing a curve for it would be a lie the charts would draw.
//
// `sigma` > 0 is a stated 1σ for a DERIVED entry, carried as an assumed-input σ (SigmaKind) because it
// comes from the derivation's stated assumptions rather than from the device; a device reading
// carries none (the device states no uncertainty, and absent is "not characterised").
QJsonObject lmMetricEntry(const QString &key, const QString &label, const QString &unit,
                          double value, qint64 impactUs, double sigma = 0.0)
{
    QJsonObject ps{
        { QStringLiteral("phase"), int(analysis::Phase::Impact) },
        { QStringLiteral("t_us"),  impactUs },
        { QStringLiteral("value"), value },
        { QStringLiteral("band"),  QString() } };
    QJsonObject m{
        { QStringLiteral("key"),   key },
        { QStringLiteral("label"), label },
        { QStringLiteral("unit"),  unit },
        { QStringLiteral("t_us"),  QJsonArray{} },
        { QStringLiteral("value"), QJsonArray{} },
    };
    if (sigma > 0.0) {
        const int kind = int(analysis::SigmaKind::AssumedInput);
        ps.insert(QStringLiteral("sigma"), sigma);
        ps.insert(QStringLiteral("sigmaKind"), kind);
        m.insert(QStringLiteral("sigma"), sigma);
        m.insert(QStringLiteral("sigmaKind"), kind);
    }
    m.insert(QStringLiteral("phaseSamples"), QJsonArray{ ps });
    return m;
}

// The keys we DERIVE from a reading rather than read off it, and the reason they are listed
// separately from fieldDefs(). A derived quantity has no member to point at, so it cannot live in
// that table — and it must NOT be `lm.`-prefixed either, because that prefix means "the device
// said this" and the catalogue test enforces it in both directions (metric_catalogue_test §3f: no
// `lm.` descriptor may exist that no reading field can fill). These are ours, computed from the
// device's numbers, so they carry bare keys and a Device route.
//
// THE LIST EXISTS FOR THE STRIP RULE, not for the emitter. updateLaunchMonitor() drops the old
// entries before re-adding so that re-applying a reading replaces rather than duplicates, and it
// recognises them by the `lm.` prefix. A derived key has no prefix to be caught by, so a second
// pairing would have appended a second copy — silently, and only on shots a device was re-read for.
// The list is the measurement vocabulary's (Metrics/measurement.h, refactor 6.1a): stated once, with the provider's claim.
const QStringList &lmDerivedKeys()
{
    static const QStringList keys = [] {
        QStringList k;
        for (const QString &key : analysis::lmDerivedKeys()) k << key;
        return k;
    }();
    return keys;
}

// The radius the club's head swings through near the bottom, for lmLowPointFromAttack: the standard
// length of the club the shot was hit with, for the golfer's RECORDED club set, plus the arm term
// (step 1.6; step 1 brief §4.3). The club is the EFFECTIVE one — effectiveContext() v2 over the
// document's analysis context (recorded, or the legacy one for an older document), its review and its
// device club: the one club (club_vocabulary.h; refactor 6.2), exactly as the analysis takes it. So a device-only shot follows
// the correction too, and the women's set is reachable. A junior with no recorded height has no
// junior standard; their club is read from the men's set (the set does not say more), as before.
// A club the table does not know — or none — takes the mid-iron default.
double lmArcRadiusM(const QJsonObject &root)
{
    const analysis::EffectiveContext e = analysis::effectiveContext(readAnalysisContext(root).context,
                                                                    clubReviewOf(root));
    double lengthMm = e.club.standardLengthMm;
    if (!(lengthMm > 0.0) && e.clubSet == ClubSet::Junior) {
        // The club the standard length belongs to, by the rule's own precedence.
        const QString club = e.club.corrected || e.club.declared.isEmpty() ? e.club.name : e.club.declared;
        const pinpoint::ClubStandard men = pinpoint::standardClub(club, ClubSet::Men);
        lengthMm = men.valid ? men.lengthMm : 0.0;
    }
    return lengthMm > 0.0 ? lengthMm / 1000.0 + analysis::kArcArmM : analysis::kArcRadiusDefaultM;
}

// The reading back out of the raw `launchMonitor` block (lmRawBlock writes every field under its
// rawName), so a re-analysis can DERIVE again rather than carry a derived row it cannot re-check.
lm::LaunchMonitorReading lmReadingFromRaw(const QJsonObject &raw)
{
    lm::LaunchMonitorReading r;
    for (const lm::FieldDef &f : lm::fieldDefs()) {
        const QJsonValue v = raw.value(QString::fromLatin1(f.rawName));
        if (v.isDouble())
            r.*(f.member) = v.toDouble();
    }
    return r;
}

// What WE compute from a reading — bare keys, listed in lmDerivedKeys().
QJsonArray lmDerivedEntries(const lm::LaunchMonitorReading &reading, qint64 impactUs, double arcRadiusM)
{
    QJsonArray metrics;

    // Produced HERE, beside the readings, rather than in an analysis stage — and that is a fact
    // about when a launch monitor speaks, not a shortcut. A reading is paired to a swing by
    // shot_pairing AFTER the stages have run, and often after the document is already on disk, so
    // a stage would compute this from a `launchMonitor` block that was not there yet. This is the
    // first point at which the reading and the document are in the same room.
    const analysis::LmCompoundMiss cm =
        analysis::lmCompoundMiss(reading.launchDirection, reading.spinAxis, reading.faceToPath,
                                 reading.carryDistance, reading.offline);
    if (cm.has)
        metrics.append(lmMetricEntry(QStringLiteral("compoundMiss"),
                                     QStringLiteral("Compound miss"), QStringLiteral("ratio"),
                                     cm.value, impactUs));

    // The low point the device's attack angle implies (issue #92): a GC Quad does not report one,
    // and the diagnostic measure prefers this to the camera's arc fit.
    const analysis::LmLowPoint lp = analysis::lmLowPointFromAttack(reading.attackAngle, arcRadiusM);
    if (lp.has)
        metrics.append(lmMetricEntry(QStringLiteral("lowPointFromAttack"),
                                     QStringLiteral("Low point (from attack angle)"),
                                     QStringLiteral("in"), lp.value, impactUs, lp.sigma));

    return metrics;
}

QJsonArray lmMetricEntries(const lm::LaunchMonitorReading &reading, qint64 impactUs, double arcRadiusM)
{
    QJsonArray metrics;
    for (const lm::FieldDef &f : lm::fieldDefs()) {
        const auto &val = reading.*(f.member);
        if (!val)
            continue;
        metrics.append(lmMetricEntry(QString::fromLatin1(f.key), QString::fromUtf8(f.label),
                                     QString::fromUtf8(f.unit), *val, impactUs));
    }
    for (const QJsonValue &v : lmDerivedEntries(reading, impactUs, arcRadiusM))
        metrics.append(v);
    return metrics;
}

// ── The just-written document (see SwingDocWriter::takeJustWritten) ─────────────────────────────
//
// One entry, because one is all the live path needs: write a swing, replay it. Guarded by a mutex —
// the writers run on the GUI thread today, but nothing in the signature says so, and handing a
// half-assigned QJsonObject to a reader would be a nasty way to find that out.
//
// ⚠ TAKING IT CLEARS IT, and every rewrite of a document drops it. A cached root that outlived its
// file would replay yesterday's analysis of today's swing, which is worse than the parse it saves.
QMutex           g_justWrittenMutex;
QString          g_justWrittenDir;
QJsonObject      g_justWrittenRoot;

void setJustWritten(const QString &swingDir, const QJsonObject &root)
{
    QMutexLocker lk(&g_justWrittenMutex);
    g_justWrittenDir  = swingDir;
    g_justWrittenRoot = root;
}

void clearJustWritten(const QString &swingDir)
{
    QMutexLocker lk(&g_justWrittenMutex);
    if (g_justWrittenDir == swingDir) {
        g_justWrittenDir.clear();
        g_justWrittenRoot = QJsonObject{};
    }
}

// THE write. Every writer ends here: the `summary` block is rebuilt from the root being written
// (never carried over — a re-analysis hands back the whole old document, summary and all), then
// the document goes to disk as swing.ppsw, superseding any JSON-era swing.json and its sidecar.
bool saveDocument(const QString &swingDir, QJsonObject &root, QString *error)
{
    root.remove(QStringLiteral("summary"));
    root.insert(QStringLiteral("summary"), summaryJson(summaryFromRoot(root, swingDir)));
    return SwingStore::save(swingDir, root, error);
}

WriteResult result(WriteResult::Status status, const QString &error = QString())
{
    WriteResult r;
    r.status = status;
    r.error  = error;
    return r;
}

// THE read of every read-modify-write below: the document, or NoDocument (the empty-document
// guard: a document that went missing or could not be read is never replaced by a partial one),
// or ReadOnly (saved by a newer build). True when *root may be modified and written back.
bool loadForRewrite(const QString &swingDir, QJsonObject *root, WriteResult *out)
{
    QString err;
    {
        PostShotTrace::Scope trace("load");   // the whole document parsed
        *root = SwingStore::load(swingDir, &err);
    }
    if (root->isEmpty()) {
        *out = result(WriteResult::Status::NoDocument,
                      err.isEmpty() ? QStringLiteral("no swing document in %1").arg(swingDir) : err);
        return false;
    }
    return !SwingDocWriter::refusesWrite(*root, out);
}

// saveDocument as a WriteResult.
WriteResult saveAs(const QString &swingDir, QJsonObject &root)
{
    QString err;
    PostShotTrace::Scope trace("save");       // encoded, written, synced (file and folder) and renamed
    if (!saveDocument(swingDir, root, &err))
        return result(WriteResult::Status::Failed, err);
    return result(WriteResult::Status::Written);
}

} // namespace

bool SwingDocWriter::refusesWrite(const QJsonObject &root, WriteResult *out)
{
    // The one rule, the Recording loader's: the document's schema, or its analysisContext block's,
    // is newer than this build reads (recording_header.h readAnalysisContext).
    const AnalysisContextRead ctx = readAnalysisContext(root);
    if (!ctx.readOnly)
        return false;
    if (out)
        *out = result(WriteResult::Status::ReadOnly,
                      QStringLiteral("not written: %1").arg(ctx.readOnlyReason));
    return true;
}

WriteResult SwingDocWriter::writeDocument(const QString &swingDir, const QJsonObject &rawManifest,
                                          const analysis::SwingAnalysis *analysis,
                                          const QString &club, bool savePose)
{
    // A write-back hands in the document on disk: never rewrite one a newer build saved.
    WriteResult refused;
    if (refusesWrite(rawManifest, &refused))
        return refused;

    QJsonObject root = rawManifest;
    root[QStringLiteral("schema")] = QString::fromLatin1(kSwingDocSchema);   // the one schema constant

    // A declared club handed in by the caller, written into the review block. LIVE CAPTURE HANDS
    // NONE since refactor 6.2: the session's club is the captured club (capture.club.name), and a
    // seed in review.club would stand above the device club follow-club stores (the one club rule,
    // club_vocabulary.h; DEC-133). Empty ⇒ no block at all: an absent review is honest, a review
    // naming no club is not.
    //
    // SEEDS, never overwrites. `rawManifest` is a fresh capture manifest on the live path,
    // but the re-analysis path hands back the whole existing document (reanalysis_controller)
    // — replacing its review there would drop the user's rating and note along with their
    // club. Only an absent or club-less review is filled in.
    if (!club.isEmpty()) {
        QJsonObject review = root.value(QStringLiteral("review")).toObject();
        if (review.value(QStringLiteral("club")).toString().trimmed().isEmpty()) {
            review[QStringLiteral("club")] = club;
            if (!review.contains(QStringLiteral("rating"))) review[QStringLiteral("rating")] = 0;
            if (!review.contains(QStringLiteral("note")))   review[QStringLiteral("note")]   = QString();
            root[QStringLiteral("review")] = review;
        }
    }

    if (analysis) {
        // clock.t0_us = the absolute window start; serializeAnalysis uses it to
        // emit analysis t_us window-relative regardless of the source domain.
        const qint64 t0 = qint64(rawManifest.value(QStringLiteral("clock")).toObject()
                                            .value(QStringLiteral("t0_us")).toDouble());
        // THE BLOCK, COMPOSED ONCE (swingdoc_detail::analysisBlockFor, swing_doc_analysis.cpp): the DTL
        // club block's stream named from this manifest's streams[], serializeAnalysis, and the
        // whole-shot gate records re-derived from this manifest's captureIntegrity / imuIntegrity
        // blocks, which both write paths (the live join, the re-analysis write-back) set fresh before
        // calling here. The live shot's detail is projected from the same function (liveAnalysisDetail),
        // so the live map and this document cannot be composed two ways. What follows is what only a
        // write has an input for.
        QJsonObject an = swingdoc_detail::analysisBlockFor(*analysis, t0, rawManifest);
        if (!savePose) {
            // The user chose not to keep pose keypoints (Storage → Save pose keypoints). The
            // setting governs NEWLY INFERRED keypoints only (Mark, 14 Nov 2026): a track that this
            // pass REUSED from the document (analysis.path.reuse "reused" — the version-gated reuse
            // in reuse_gate.h, which re-runs inference only when the pose stage version changed —
            // Mark's pose rule) is written back as it came, so re-analysis never deletes keypoints it did
            // not re-infer. A newly inferred track is dropped, and with it the superseded one the
            // document held (the analysis block is replaced whole). The stamps go with the tracks:
            // a stamp promising a pose that is not there would send a version-gated re-analysis
            // looking for it.
            const QString reused = QStringLiteral("reused");
            const bool keepPose    = analysis->path.reuse.value(QStringLiteral("pose")) == reused;
            const bool keepPoseDtl = analysis->path.reuse.value(QStringLiteral("poseDtl")) == reused;
            QJsonObject ver = an.value(QStringLiteral("versions")).toObject();
            if (!keepPose) {
                an.remove(QStringLiteral("pose2d"));
                ver.remove(QStringLiteral("pose"));
            }
            if (!keepPoseDtl) {
                an.remove(QStringLiteral("poseDtl"));
                ver.remove(QStringLiteral("poseDtl"));
            }
            if (an.contains(QStringLiteral("versions"))) an.insert(QStringLiteral("versions"), ver);
        }

        // CARRIES THE LAUNCH MONITOR ROWS ACROSS, for the same reason the review block above
        // seeds rather than overwrites: re-analysis owns what it computed and must not evict
        // what it cannot recompute.
        //
        // No stage produces an `lm.` row. A reading is paired to a swing by shot_pairing AFTER
        // the stages have run (see lmMetricEntries), so updateLaunchMonitor() writes the entries
        // straight into the document and the document is the ONLY copy. serializeAnalysis
        // rebuilds metrics[] from the stages alone, so replacing the block wholesale deleted
        // them — silently, and on every re-analysis. The device's numbers do survive in the raw
        // `launchMonitor` block, but that block is provenance with no reader (see the reload
        // path, which takes `kind` out of it and nothing else), so the session board, the tiles
        // and every `lm.` grade went dark while the file still looked complete.
        const QJsonArray prior = rawManifest.value(QStringLiteral("analysis")).toObject()
                                            .value(QStringLiteral("metrics")).toArray();
        if (!prior.isEmpty()) {
            QJsonArray metrics = an[QStringLiteral("metrics")].toArray();

            // FRESH WINS. The catalogue does declare these keys (LaunchMonitorProvider and
            // LaunchMonitorDerivedProvider), so a stage that starts emitting one must replace
            // the carried row rather than sit beside it as a duplicate the resolver would pick
            // between arbitrarily. Today nothing does, and this loop carries everything.
            QSet<QString> computed;
            for (const QJsonValue &v : metrics)
                computed.insert(v.toObject().value(QStringLiteral("key")).toString());

            // THE DERIVED ROWS ARE DERIVED AGAIN, from the raw block, when there is one: a derivation
            // that changes (or is new — lowPointFromAttack, issue #92) must reach swings paired before
            // it, and carrying the old row would freeze it. With no raw block they are carried as before.
            const QJsonObject lmRaw = rawManifest.value(QStringLiteral("launchMonitor")).toObject();
            const bool rederive = !lmRaw.isEmpty();
            for (const QJsonValue &v : prior) {
                const QString key = v.toObject().value(QStringLiteral("key")).toString();
                const bool derived = lmDerivedKeys().contains(key);
                if (!key.startsWith(QStringLiteral("lm.")) && !derived)
                    continue;
                if (computed.contains(key) || (derived && rederive))
                    continue;
                // Verbatim, phaseSample timestamp included. updateLaunchMonitor() anchored it at
                // capture.impactUs, which re-analysis does not rewrite, so re-stamping would
                // reproduce the value already there — and a reading is located by its phase tag
                // rather than its timestamp in any case.
                metrics.append(v);
            }
            if (rederive) {
                const qint64 impactUs =
                    qMax<qint64>(0, qint64(rawManifest.value(QStringLiteral("capture")).toObject()
                                               .value(QStringLiteral("impactUs")).toDouble(-1)));
                for (const QJsonValue &v : lmDerivedEntries(lmReadingFromRaw(lmRaw), impactUs,
                                                            lmArcRadiusM(rawManifest)))
                    if (!computed.contains(v.toObject().value(QStringLiteral("key")).toString()))
                        metrics.append(v);
            }
            an[QStringLiteral("metrics")] = metrics;
        }
        // …AND ITS GATE RECORDS, which are not carried but re-derived: serializeAnalysis rebuilt
        // analysis.gates from the stages alone, and the launch monitor's records come from the drop
        // list its `launchMonitor` block stores (lm_gates.h; trust stage 6). No block, no records.
        lm::applyLmGates(an, rawManifest.value(QStringLiteral("launchMonitor")).toObject());
        root[QStringLiteral("analysis")] = an;
    }

    // The summary rides inside the document (saveDocument), so indexing the swing for the
    // session picker costs nothing extra here.
    const WriteResult w = saveAs(swingDir, root);
    if (!w.ok())
        return w;
    // …and keep the document itself for whoever reads it next, which on the live path is the replay,
    // microseconds later. See takeJustWritten(): this saves a fetch and a full re-parse of the ~28 MB
    // we just serialised, on the GUI thread, off the share.
    setJustWritten(swingDir, root);
    return w;
}

namespace {

// The `origin` block, as JSON.  Optional keys are omitted rather than written
// empty: a stream that has not been committed has no `committedAt`, and saying
// so with an absent key is honest where an empty string is noise.
QJsonObject originToJson(const SwingDocWriter::StreamOrigin &o)
{
    QJsonObject j;
    j[QStringLiteral("transport")]    = o.transport;
    j[QStringLiteral("peerId")]       = o.peerId;
    j[QStringLiteral("sessionId")]    = o.sessionId;
    j[QStringLiteral("captureId")]    = o.captureId;
    if (!o.streamId.isEmpty())     j[QStringLiteral("streamId")]     = o.streamId;
    j[QStringLiteral("transfer")]     = o.transfer;
    if (!o.completeness.isEmpty()) j[QStringLiteral("completeness")] = o.completeness;
    // 7.3b — a reason belongs to an absent Capture and to nothing else.
    if (!o.absentReason.isEmpty()) j[QStringLiteral("reason")]       = o.absentReason;
    if (!o.committedAt.isEmpty())  j[QStringLiteral("committedAt")]  = o.committedAt;
    return j;
}

// The same `frames` shape every other video stream uses, so no reader needs to
// learn a second one.
QJsonObject framesToJson(const QVector<qint64> &tUs)
{
    QJsonArray a;
    for (qint64 t : tUs) a.append(double(t));
    QJsonObject f;
    f[QStringLiteral("count")] = int(tUs.size());
    f[QStringLiteral("t_us")]  = a;
    return f;
}

// The frames block, marked with the clock its times run on when that is not the window's
// (`frames.clock`, read by SwingStore::streamOnWindowClock).
QJsonObject framesOnClock(const QVector<qint64> &tUs, const QString &clock)
{
    QJsonObject f = framesToJson(tUs);
    if (!clock.isEmpty()) f[QStringLiteral("clock")] = clock;
    return f;
}

}  // namespace

WriteResult SwingDocWriter::updateStreamOrigin(const QString &swingDir, const StreamArrival &arrival)
{
    const QString         &alias       = arrival.alias;
    const StreamOrigin    &origin      = arrival.origin;
    const QString         &fileName    = arrival.fileName;
    const QVector<qint64> &frameTUs    = arrival.frameTUs;
    const double           playbackFps = arrival.playbackFps;
    const QString         &framesClock = arrival.framesClock;
    if (alias.isEmpty())
        return result(WriteResult::Status::Failed, QStringLiteral("updateStreamOrigin needs a stream alias"));

    QJsonObject root;
    WriteResult refused;
    if (!loadForRewrite(swingDir, &root, &refused))
        return refused;

    QJsonArray streams = root.value(QStringLiteral("streams")).toArray();

    // Idempotent: REPLACE the block on the matching element rather than append a
    // second one.  A transfer moves through several states -- requested, arriving,
    // then complete or absent -- and each is an update to one statement, not a new
    // statement beside the last.
    bool found = false;
    for (int i = 0; i < streams.size(); ++i) {
        QJsonObject el = streams.at(i).toObject();
        if (el.value(QStringLiteral("alias")).toString() != alias) continue;
        el[QStringLiteral("origin")] = originToJson(origin);
        if (!fileName.isEmpty()) el[QStringLiteral("file")] = fileName;
        if (!frameTUs.isEmpty()) el[QStringLiteral("frames")] = framesOnClock(frameTUs, framesClock);
        if (playbackFps > 0.0)
            el[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("fps"), playbackFps}};
        streams.replace(i, el);
        found = true;
        break;
    }

    if (!found) {
        // A clip that has been ASKED for but has not arrived still deserves a row:
        // the swing knows a phone was expected to contribute one.
        //
        // ⚠ NO `file` KEY, deliberately.  DiskReplaySource and SwingDiskLoader both
        // skip a video element whose file is missing, so a pending element is inert
        // to every existing consumer until the bytes actually land.
        QJsonObject el;
        el[QStringLiteral("kind")]   = QStringLiteral("video");
        el[QStringLiteral("alias")]  = alias;
        el[QStringLiteral("origin")] = originToJson(origin);
        if (!fileName.isEmpty()) el[QStringLiteral("file")] = fileName;
        if (!frameTUs.isEmpty()) el[QStringLiteral("frames")] = framesOnClock(frameTUs, framesClock);
        if (playbackFps > 0.0)
            el[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("fps"), playbackFps}};
        streams.append(el);
    }
    root[QStringLiteral("streams")] = streams;

    const WriteResult w = saveAs(swingDir, root);
    // This document just changed underneath any copy writeDocument() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    if (w.ok())
        clearJustWritten(swingDir);
    return w;
}

WriteResult SwingDocWriter::editReview(const QString &swingDir, const ReviewEdit &edit)
{
    QJsonObject root;
    WriteResult refused;
    if (!loadForRewrite(swingDir, &root, &refused))
        return refused;

    // Additive "review" block — additive, readers ignore unknown keys. Club is the user's chosen
    // club for the shot (seeded at capture with the session's club, writeDocument).
    //
    // MERGED INTO, NOT REPLACED: the block holds the popover's rating, note and club AND the launch
    // monitor board's annotations (excluded, clubOverride), and rewriting it from one group alone
    // would silently un-exclude a shot every time its stars changed.
    QJsonObject review = root.value(QStringLiteral("review")).toObject();
    const QJsonObject before = review;

    // A rating / note / club edit always rewrites (and so re-indexes) the document, as it always has.
    const bool always = edit.rating || edit.note || edit.club;
    if (edit.rating) review[QStringLiteral("rating")] = std::clamp(*edit.rating, 0, 5);
    if (edit.note)   review[QStringLiteral("note")]   = *edit.note;
    if (edit.club)   review[QStringLiteral("club")]   = *edit.club;

    // The annotations are written only when set; the default REMOVES the key, so "absent" keeps
    // meaning "never annotated" and a shot restored to the board reads back exactly like one never
    // touched.
    if (edit.excluded) {
        if (*edit.excluded) review[QStringLiteral("excluded")] = true;
        else                review.remove(QStringLiteral("excluded"));
    }
    if (edit.clubOverride) {
        const QString ov = edit.clubOverride->trimmed();
        if (!ov.isEmpty()) review[QStringLiteral("clubOverride")] = ov;
        else               review.remove(QStringLiteral("clubOverride"));
    }
    // The shot's own golfer input (IM-S2-3; FILE-SHOT-02): written only when set, and removed when set
    // back to none, like the annotations — so a document no golfer gave an intent or a correction stays
    // exactly as it was, and an old document without the keys reads as having none.
    // keepEmptyGolferInput (step 2.5, J-33): the cleared answer is kept as an empty value while
    // diagnostics.json's older copy would otherwise be read again in its place.
    if (edit.shotIntent) {
        const QString intent = edit.shotIntent->trimmed();
        if (!intent.isEmpty() || edit.keepEmptyGolferInput) review[QStringLiteral("shotIntent")] = intent;
        else                                                review.remove(QStringLiteral("shotIntent"));
    }
    if (edit.labelCorrections) {
        QJsonArray list;
        for (const LabelCorrection &c : *edit.labelCorrections)
            list.append(QJsonObject{ { QStringLiteral("family"), c.family }, { QStringLiteral("from"), c.from },
                                     { QStringLiteral("to"), c.to } });
        if (!list.isEmpty() || edit.keepEmptyGolferInput) review[QStringLiteral("labelCorrections")] = list;
        else                                              review.remove(QStringLiteral("labelCorrections"));
    }

    if (!always && review == before)
        return result(WriteResult::Status::Unchanged);   // nothing to say — and no 28 MB rewrite to say it
    // A block that held nothing but annotations, now cleared, goes away entirely rather than
    // lingering as an empty object a reader would have to interpret.
    if (review.isEmpty())
        root.remove(QStringLiteral("review"));
    else
        root[QStringLiteral("review")] = review;

    // Club, stars, note and both annotations live in the summary, which saveDocument rebuilds.
    const WriteResult w = saveAs(swingDir, root);
    // This document just changed underneath any copy writeDocument() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    if (w.ok())
        clearJustWritten(swingDir);
    return w;
}

ShotReview SwingDocWriter::readReview(const QString &swingDir)
{
    ShotReview out;
    QString err;
    // The review is small and sits in the document's root: read the root alone, never the pose tracks.
    const QJsonObject root = SwingStore::loadParts(swingDir, {}, &err);
    if (root.isEmpty()) {
        out.error = err.isEmpty() ? QStringLiteral("no swing document in %1").arg(swingDir) : err;
        return out;
    }
    const QJsonObject review = root.value(QStringLiteral("review")).toObject();
    out.ok         = true;
    out.hasShotIntent       = review.contains(QStringLiteral("shotIntent"));
    out.hasLabelCorrections = review.contains(QStringLiteral("labelCorrections"));
    out.shotIntent = review.value(QStringLiteral("shotIntent")).toString();
    out.lmExcluded     = review.value(QStringLiteral("excluded")).toBool(false);
    out.lmClubOverride = review.value(QStringLiteral("clubOverride")).toString();
    for (const QJsonValue &v : review.value(QStringLiteral("labelCorrections")).toArray()) {
        const QJsonObject c = v.toObject();
        out.labelCorrections.append({ c.value(QStringLiteral("family")).toString(),
                                      c.value(QStringLiteral("from")).toString(),
                                      c.value(QStringLiteral("to")).toString() });
    }
    return out;
}

WriteResult SwingDocWriter::writeArchiveStub(const QString &swingDir, const QJsonObject &stub)
{
    // Read-only as for every rewrite: a newer build's document is never thinned by this one (its fields
    // would be lost from the stub). The stub is the document thinned, its schema and analysisContext
    // kept (SessionArchiver's stubOf), so the stub answers for the document without a second parse.
    if (!SwingStore::hasDocument(swingDir))
        return result(WriteResult::Status::NoDocument, QStringLiteral("no swing document in %1").arg(swingDir));
    WriteResult refused;
    if (stub.isEmpty())
        return result(WriteResult::Status::Failed, QStringLiteral("an empty stub for %1").arg(swingDir));
    if (SwingDocWriter::refusesWrite(stub, &refused))
        return refused;
    // The stub is written AS GIVEN (SwingStore's bytes, no summary rebuild): it carries the summary the
    // document already held, which is what the session list keeps showing while the shot is archived.
    QString err;
    if (!SwingStore::save(swingDir, stub, &err))
        return result(WriteResult::Status::Failed, err);
    clearJustWritten(swingDir);
    return result(WriteResult::Status::Written);
}

WriteResult SwingDocWriter::recordBackfilledContext(const QString &swingDir, const AthleteRecordFacts &facts,
                                                    bool dryRun)
{
    QJsonObject root;
    WriteResult refused;
    if (!loadForRewrite(swingDir, &root, &refused))
        return refused;
    // Recorded at capture (or by an earlier backfill): a recorded input is never regenerated.
    if (root.contains(QStringLiteral("analysisContext")))
        return result(WriteResult::Status::Unchanged);
    const QJsonObject block = analysisContextJson(backfilledAnalysisContext(root, facts.heightM, facts.sex, facts.dobIso));
    if (block == analysisContextJson(legacyAnalysisContext(root)))
        return result(WriteResult::Status::Unchanged);   // nothing to add to what it reads today
    if (dryRun)
        return result(WriteResult::Status::Written);     // WOULD be written
    root[QStringLiteral("analysisContext")] = block;
    const WriteResult w = saveAs(swingDir, root);
    if (w.ok())
        clearJustWritten(swingDir);
    return w;
}

WriteResult SwingDocWriter::updateLaunchMonitor(const QString &swingDir,
                                                const lm::LaunchMonitorReading &reading,
                                                const QJsonObject &captureClub,
                                                bool *clubReplaced)
{
    if (clubReplaced)
        *clubReplaced = false;
    QJsonObject root;
    WriteResult refused;
    if (!loadForRewrite(swingDir, &root, &refused))
        return refused;

    // FOLLOW-CLUB (R-REC-2(a), R-CLUB-1, DEC-133; refactor 6.2): the club the device named is stored BESIDE the captured
    // club, as its own source fact, capture.deviceClub (capture.club's shape). The captured club (capture.club and the
    // analysis context) and the golfer's review are never touched. Stored only when it names another club than the
    // captured one, and once per club. *clubReplaced says whether it changed the shot's ONE club (club_vocabulary.h:
    // no correction and no popover club stand above it), so the caller re-analyses with it.
    if (!captureClub.isEmpty()) {
        const QString named = captureClub.value(QStringLiteral("name")).toString().trimmed();
        const ClubFacts before = clubFactsOfDocument(root);
        if (!named.isEmpty() && canonicalClubId(named) != canonicalClubId(before.captured)
            && canonicalClubId(named) != canonicalClubId(before.device)) {
            QJsonObject cap = root[QStringLiteral("capture")].toObject();
            cap[QStringLiteral("deviceClub")] = captureClub;
            root[QStringLiteral("capture")] = cap;
            if (clubReplaced)
                *clubReplaced = canonicalClubId(effectiveClub(clubFactsOfDocument(root)))
                             != canonicalClubId(effectiveClub(before));
        }
    }

    // ── The raw block: what the device said, in full ────────────────────────
    // Everything, including the columns no metric consumes. The reading is already
    // in catalogue units, so this and the metric entries below cannot disagree —
    // storing the file's own units here as well would invite exactly that.
    root[QStringLiteral("launchMonitor")] = lmRawBlock(reading);

    // ── The metric entries ──────────────────────────────────────────────────
    if (root.contains(QStringLiteral("analysis"))) {
        QJsonObject an = root[QStringLiteral("analysis")].toObject();

        // Impact in the window-relative domain every analysis t_us uses. -1 (or a
        // missing capture block) means the shot never resolved an impact; anchor at
        // 0 rather than dropping the readings, since a reduction "at p7" locates the
        // sample by its phase tag and not by its timestamp.
        const qint64 impactUs =
            qMax<qint64>(0, qint64(root[QStringLiteral("capture")].toObject()
                                       .value(QStringLiteral("impactUs")).toDouble(-1)));

        // Drop any `lm.` entries already present so re-applying replaces rather than
        // duplicates. Bare keys are untouched — that separation is the whole point — EXCEPT the
        // handful we derive from the reading ourselves, which are ours and bare and would
        // otherwise accumulate one copy per pairing. See lmDerivedKeys().
        QJsonArray metrics;
        for (const QJsonValue &v : an[QStringLiteral("metrics")].toArray()) {
            const QString key = v.toObject().value(QStringLiteral("key")).toString();
            if (key.startsWith(QStringLiteral("lm.")) || lmDerivedKeys().contains(key))
                continue;
            metrics.append(v);
        }

        for (const QJsonValue &v : lmMetricEntries(reading, impactUs, lmArcRadiusM(root)))
            metrics.append(v);

        an[QStringLiteral("metrics")] = metrics;
        // The fields this reading dropped, as gate records (lm_gates.h): any from an earlier
        // pairing replaced, never accumulated, exactly as the lm. rows above.
        lm::applyLmGates(an, root[QStringLiteral("launchMonitor")].toObject());
        root[QStringLiteral("analysis")] = an;
    }

    // The summary (the chips include the new lm rows) is rebuilt by saveDocument. The phase grid
    // sidecar is not rewritten here — this rewrite moves the document's size and mtime, which is
    // its guard, so it is regenerated on demand; and it MUST be, since the readings we just added
    // are new rows in it.
    const WriteResult w = saveAs(swingDir, root);
    if (!w.ok()) {
        if (clubReplaced)
            *clubReplaced = false;   // nothing was replaced on disk
        return w;
    }
    // This document just changed underneath any copy writeDocument() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    clearJustWritten(swingDir);
    return w;
}

// The same JSON, a number counted the same when it differs only in its last bits: a derivation
// re-run on another build can land one ulp away, and that is not a change worth rewriting a
// document (and regrading its shot) for.
static bool sameJson(const QJsonValue &a, const QJsonValue &b)
{
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        return x == y || std::abs(x - y) <= 1e-12 * std::max(std::abs(x), std::abs(y));
    }
    if (a.type() != b.type()) return false;
    if (a.isArray()) {
        const QJsonArray x = a.toArray(), y = b.toArray();
        if (x.size() != y.size()) return false;
        for (qsizetype i = 0; i < x.size(); ++i)
            if (!sameJson(x.at(i), y.at(i))) return false;
        return true;
    }
    if (a.isObject()) {
        const QJsonObject x = a.toObject(), y = b.toObject();
        if (x.keys() != y.keys()) return false;
        for (auto it = x.begin(); it != x.end(); ++it)
            if (!sameJson(it.value(), y.value(it.key()))) return false;
        return true;
    }
    return a == b;
}

WriteResult SwingDocWriter::refreshLaunchMonitorRows(const QString &swingDir, bool dryRun)
{
    QJsonObject root;
    WriteResult refused;
    if (!loadForRewrite(swingDir, &root, &refused))
        return refused;
    const QJsonObject raw = root.value(QStringLiteral("launchMonitor")).toObject();
    if (raw.isEmpty() || !root.contains(QStringLiteral("analysis")))
        return result(WriteResult::Status::Unchanged);

    // The same rows, in the same order, updateLaunchMonitor() writes: everything not ours kept
    // as it was, then the readings and the derived rows from the raw block, anchored at impact.
    QJsonObject an = root[QStringLiteral("analysis")].toObject();
    const QJsonArray before = an[QStringLiteral("metrics")].toArray();
    QJsonArray metrics;
    for (const QJsonValue &v : before) {
        const QString key = v.toObject().value(QStringLiteral("key")).toString();
        if (key.startsWith(QStringLiteral("lm.")) || lmDerivedKeys().contains(key))
            continue;
        metrics.append(v);
    }
    const qint64 impactUs =
        qMax<qint64>(0, qint64(root[QStringLiteral("capture")].toObject()
                                   .value(QStringLiteral("impactUs")).toDouble(-1)));
    for (const QJsonValue &v : lmMetricEntries(lmReadingFromRaw(raw), impactUs, lmArcRadiusM(root)))
        metrics.append(v);
    if (sameJson(metrics, before))
        return result(WriteResult::Status::Unchanged);
    if (dryRun)
        return result(WriteResult::Status::Written);   // WOULD be

    an[QStringLiteral("metrics")] = metrics;
    lm::applyLmGates(an, raw);
    root[QStringLiteral("analysis")] = an;
    const WriteResult w = saveAs(swingDir, root);
    if (w.ok())
        clearJustWritten(swingDir);
    return w;
}

WriteResult SwingDocWriter::writeDeviceOnlySwing(const QString &swingDir,
                                                 const lm::LaunchMonitorReading &reading,
                                                 const DeviceOnlyMeta &meta)
{
    if (!reading.hasAnyValue())
        return result(WriteResult::Status::Failed,
                      QStringLiteral("refusing to write a shot from a reading with no values"));

    const QDateTime when = meta.wallclockMs > 0
                               ? QDateTime::fromMSecsSinceEpoch(meta.wallclockMs, QTimeZone::UTC)
                               : QDateTime::currentDateTimeUtc();

    // The header: the ONE header writer (recording_header.h), with what a device-only shot knows.
    RecordingHeader header;
    // The shot kind (R-REC-8; DEC-44, DEC-87), recorded only for a standalone shot: nothing can file
    // a stream into its folder later, so device-only is true for good. A rescue (a failed camera shot's
    // folder) may still gain a phone clip, so its kind is left to the content rule (recording_header.h).
    if (meta.standalone)
        header.kind = recording::ShotKind::DeviceOnly;
    header.swing   = RecordingHeader::Identity{ meta.swingId, meta.swingIndex };
    // No handedness, no height: the meta does not carry them, and none is invented.
    header.athlete = RecordingHeader::Athlete{ meta.athleteName, meta.athleteUuid, std::nullopt, 0.0 };
    header.sessionDir = meta.sessionId;   // the session FOLDER name
    // t0_us is 0, not a buffer instant: there IS no buffer. Every t_us in this document
    // is therefore already window-relative, which is the domain every reader expects.
    header.clock = RecordingHeader::Clock{ 0, when };
    // A zero-length window. Not a placeholder for a window we failed to capture — there
    // was nothing to capture, and a fabricated span would put a scrubber on a shot with
    // no frames behind it.
    header.window = RecordingHeader::Window{ 0, 0 };
    header.capture = QJsonObject{
        { QStringLiteral("sessionType"), meta.sessionType },
        // Names the launch monitor as what saw this shot, so a reader never has to infer
        // it from the absence of streams.
        { QStringLiteral("shotSource"),  QStringLiteral("launchMonitor") },
        // -1 is the schema's existing "impact unknown" sentinel, and it is the honest
        // value: the device tells us a ball was struck, never when. The manufactured
        // phase below sits at 0 and this stays -1 deliberately — the two answer
        // different questions and collapsing them would hide which one we know.
        { QStringLiteral("impactUs"),    -1 },
    };
    // No analysis context: the meta carries no athlete facts or bag (the launch monitor's caller
    // fills one when it has them, step 1.4/1.5), so the shot reads as the legacy context does.
    // EMPTY streams, and that is the fact rather than a failure. hasVideo is derived from
    // this, so the picker and the carousel already read it correctly. No thumbnail key at all:
    // an absent block is what summaryFromRoot reads as "no thumbnail", where an empty one would
    // point at a file that was never written.
    QJsonObject root = recordingHeaderJson(header, QJsonArray{});

    root[QStringLiteral("launchMonitor")] = lmRawBlock(reading);

    // The club is the app's selection, not the device's code — the same rule the
    // fold-in path follows, and what a normative corridor resolves through.
    if (!meta.club.isEmpty())
        root[QStringLiteral("review")] = QJsonObject{
            { QStringLiteral("rating"), 0 },
            { QStringLiteral("note"),   QString() },
            { QStringLiteral("club"),   meta.club },
        };

    // ── analysis: metrics and ONE phase, and nothing else ───────────────────
    //
    // No score: nothing scored it. No pose, no club, no ball, no bindings, no
    // segmentation. Omitting them is what stops a reader mistaking this for an analysed
    // swing whose analysis came out empty.
    QJsonObject an;
    an[QStringLiteral("schema")]  = QStringLiteral("pinpoint.analysis/3");
    // Window time, and it says so (IM-2), as serializeAnalysis does: t0_us is 0 here, so every
    // time below is already window-relative.
    an[QStringLiteral("timeBase")] = QString::fromLatin1(analysis::kAnalysisTimeBaseWindow);
    an[QStringLiteral("tier")]    = int(analysis::ReconstructionTier::Angles2D);
    an[QStringLiteral("metrics")] = lmMetricEntries(reading, /*impactUs*/ 0, lmArcRadiusM(root));
    an[QStringLiteral("phases")]  = QJsonArray{ QJsonObject{
        { QStringLiteral("phase"),   int(analysis::Phase::Impact) },
        { QStringLiteral("t_us"),    0 },
        // The EVENT is certain — a ball was struck and the device measured it. Only its
        // instant is unknown, and that is stated by capture.impactUs, not here.
        { QStringLiteral("conf"),    1.0 },
        { QStringLiteral("segment"), int(analysis::SegmentRole::Unknown) },
    } };
    // Gate records: the only gates a device-only shot has are the fields its import dropped
    // (lm_gates.h). Stamped like every analysis (trust stage 6, kGatesVersion), so an empty array
    // reads "nothing was dropped" — no camera reading exists here for it to vouch for.
    an[QStringLiteral("gatesVersion")] = analysis::kGatesVersion;
    lm::applyLmGates(an, root[QStringLiteral("launchMonitor")].toObject());
    root[QStringLiteral("analysis")] = an;

    if (!QDir().mkpath(swingDir))
        return result(WriteResult::Status::Failed, QStringLiteral("cannot create %1").arg(swingDir));

    const WriteResult w = saveAs(swingDir, root);
    // This document just changed underneath any copy writeDocument() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    if (w.ok())
        clearJustWritten(swingDir);
    return w;
}

SwingStore::ConvertResult SwingDocWriter::convertToPpsw(const QString &swingDir, bool deleteJson)
{
    // A JSON-era document a newer build wrote (no build ever has; the rule is every write's) is not
    // rewritten. This costs one more parse of the JSON, on a conversion that runs once per swing.
    if (SwingStore::info(swingDir).format == SwingStore::Format::Json) {
        WriteResult refused;
        if (refusesWrite(SwingStore::load(swingDir), &refused)) {
            SwingStore::ConvertResult r;
            r.outcome = SwingStore::ConvertResult::Outcome::Failed;
            r.error   = refused.error;
            return r;
        }
    }
    return SwingStore::convertDir(
        swingDir,
        [](const QJsonObject &root, const QString &dir) { return summaryJson(summaryFromRoot(root, dir)); },
        deleteJson);
}

QJsonObject SwingDocWriter::takeJustWritten(const QString &swingDir)
{
    QMutexLocker lk(&g_justWrittenMutex);
    if (swingDir.isEmpty() || g_justWrittenDir != swingDir)
        return {};
    // Hand it over and forget it: one reader, once. A second caller reads the file, which is the
    // same document — the cache is an optimisation, never a source of truth.
    const QJsonObject root = g_justWrittenRoot;
    g_justWrittenDir.clear();
    g_justWrittenRoot = QJsonObject{};
    return root;
}

} // namespace pinpoint
