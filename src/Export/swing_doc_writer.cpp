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

// swing_doc_writer.cpp — writing a swing document: SwingDocWriter (writeSwingJson,
// updateStreamOrigin, updateReview, updateShotAnnotations, updateLaunchMonitor,
// refreshLaunchMonitorRows, writeDeviceOnlySwing, convertToPpsw, takeJustWritten), the one
// write every writer ends in (saveDocument), the launch monitor blocks and their derived rows,
// and the two integrity blocks (imuIntegrityJson / applyImuIntegrity, captureIntegrityJson /
// applyCaptureIntegrity). The `analysis` block itself is built in swing_doc_analysis.cpp and the
// `summary` block by swing_doc_reader.cpp's summary builders (swing_doc_internal.h); the public
// API is swing_doc.h. Split out of swing_doc.cpp verbatim (refactor step 1.0, 14 Nov 2026).

#include "swing_doc.h"
#include "swing_doc_internal.h"

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
#include "../IMU/imu_refusion_check.h"
#include "../Analysis/capture_integrity_check.h"
#include "../Analysis/manifest_gates.h"            // the integrity blocks → analysis.gates (shot.*)
#include "../LaunchMonitor/lm_inferred_reads.h"
#include "../LaunchMonitor/lm_gates.h"              // the import's dropped fields → analysis.gates
#include "../LaunchMonitor/lm_device.h"             // connector vs device: launchMonitor.device
#include "../Analysis/swing_analysis.h"
#include "../Analysis/uncertainty_json.h"         // σ fields — one spelling, four paths
#include "../Club/dtl_shaft_json.h"                // dtlStreamName — the DTL club block names its stream
#include "../Core/club_standards.h"            // lmArcRadiusM — the club's standard length
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
const QStringList &lmDerivedKeys()
{
    static const QStringList keys = { QStringLiteral("compoundMiss"),
                                      QStringLiteral("lowPointFromAttack") };
    return keys;
}

// The radius the club's head swings through near the bottom, for lmLowPointFromAttack: the standard
// length of the club this document declares, for the golfer's set, plus the arm term. A club the
// table does not know — or none — takes the mid-iron default. A junior is read from the men's or
// women's set: their own lengths need a height this document may not carry, and the σ on R (±10 %)
// is wider than the difference at the lengths juniors play.
double lmArcRadiusM(const QJsonObject &root)
{
    const QString club = swingDocDeclaredClub(root);
    const QString sex  = root.value(QStringLiteral("athlete")).toObject()
                             .value(QStringLiteral("sex")).toString();
    const pinpoint::ClubStandard st = pinpoint::standardClub(club, pinpoint::clubSetFor(sex, /*junior*/ false));
    return st.valid ? st.lengthMm / 1000.0 + analysis::kArcArmM : analysis::kArcRadiusDefaultM;
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

} // namespace

bool SwingDocWriter::writeSwingJson(const QString &swingDir, const QJsonObject &rawManifest,
                                    const analysis::SwingAnalysis *analysis, QString *error,
                                    const QString &club, bool savePose)
{
    QJsonObject root = rawManifest;
    root[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/2");

    // The club the session was on when the shot happened, seeded into the review block the
    // same way writeDeviceOnlySwing() seeds it. capture.club.name already records it, and
    // swingDocClub() falls back to that, so this is belt-and-braces rather than the only
    // copy — but it puts the answer where updateReview() will later replace it, so a user
    // correction and a capture-time pick live at one key instead of two that can disagree.
    // Empty ⇒ no block at all: an absent review is honest, a review naming no club is not.
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
        // The DTL club block names its stream as SwingLab's club_dtl.json does — matched
        // on the recorded serial against this document's own streams[]. No serial ⇒ no name:
        // dtlStreamName's first-video-stream fall-back is SwingLab's, and here it would name
        // the face-on camera.
        QString dtlAlias, dtlFile;
        if (analysis->versions.shaftDtl > 0 && !analysis->shaftDtl.streamSerial.isEmpty())
            analysis::dtlStreamName(rawManifest, analysis->shaftDtl.streamSerial, &dtlAlias, &dtlFile);
        QJsonObject an = serializeAnalysis(*analysis, t0, dtlAlias, dtlFile);
        // THE WHOLE-SHOT RECORDS (manifest_gates.h; trust stage 6): re-derived on every write from
        // this manifest's captureIntegrity / imuIntegrity blocks, which both write paths (the live
        // join, the re-analysis write-back) set fresh before calling here. serializeAnalysis rebuilt
        // analysis.gates from the stages alone, so appending cannot accumulate.
        if (an.contains(QStringLiteral("gatesVersion"))) {
            QJsonArray g = an.value(QStringLiteral("gates")).toArray();
            for (const QJsonValue &v : analysis::gateRecordsToJson(analysis::manifestGatesOf(rawManifest)))
                g.append(v);
            an[QStringLiteral("gates")] = g;
        }
        if (!savePose) {
            // The user chose not to keep pose keypoints (Storage → Save pose keypoints). The
            // stamps go with the tracks: a stamp promising a pose that is not there would send a
            // version-gated re-analysis looking for it.
            an.remove(QStringLiteral("pose2d"));
            an.remove(QStringLiteral("poseDtl"));
            QJsonObject ver = an.value(QStringLiteral("versions")).toObject();
            ver.remove(QStringLiteral("pose"));
            ver.remove(QStringLiteral("poseDtl"));
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
    if (!saveDocument(swingDir, root, error))
        return false;
    // …and keep the document itself for whoever reads it next, which on the live path is the replay,
    // microseconds later. See takeJustWritten(): this saves a fetch and a full re-parse of the ~28 MB
    // we just serialised, on the GUI thread, off the share.
    setJustWritten(swingDir, root);
    return true;
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

bool SwingDocWriter::updateStreamOrigin(const QString &swingDir, const QString &alias,
                                        const StreamOrigin &origin, QString *error,
                                        const QString &fileName,
                                        const QVector<qint64> &frameTUs,
                                        double playbackFps,
                                        const QString &framesClock)
{
    if (alias.isEmpty()) {
        if (error) *error = QStringLiteral("updateStreamOrigin needs a stream alias");
        return false;
    }

    QJsonObject root = SwingStore::load(swingDir, error);
    if (root.isEmpty())
        return false;

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

    if (!saveDocument(swingDir, root, error))
        return false;
    // This document just changed underneath any copy writeSwingJson() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    clearJustWritten(swingDir);
    return true;
}

bool SwingDocWriter::updateReview(const QString &swingDir, int rating, const QString &note,
                                  const QString &club, QString *error)
{
    QJsonObject root = SwingStore::load(swingDir, error);
    if (root.isEmpty())
        return false;

    // Additive "review" block — additive, readers ignore unknown keys. Club is
    // the user's chosen club for the shot (until capture-time club selection
    // exists it starts as the "DRIVER" stub and is only ever set here).
    //
    // MERGED INTO, NOT REPLACED: the block also holds the launch monitor board's annotations
    // (excluded, clubOverride — updateShotAnnotations), and rewriting it from these three
    // fields alone would silently un-exclude a shot every time its stars changed.
    QJsonObject review = root.value(QStringLiteral("review")).toObject();
    review[QStringLiteral("rating")] = std::clamp(rating, 0, 5);
    review[QStringLiteral("note")]   = note;
    review[QStringLiteral("club")]   = club;
    root[QStringLiteral("review")] = review;

    // Club, stars and note live in the summary, which saveDocument rebuilds from this root.
    if (!saveDocument(swingDir, root, error))
        return false;
    // This document just changed underneath any copy writeSwingJson() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    clearJustWritten(swingDir);
    return true;
}

bool SwingDocWriter::updateShotAnnotations(const QString &swingDir, bool excluded,
                                           const QString &clubOverride, QString *error)
{
    QJsonObject root = SwingStore::load(swingDir, error);
    if (root.isEmpty())
        return false;

    QJsonObject review = root.value(QStringLiteral("review")).toObject();
    const QJsonObject before = review;

    // Written only when set; the default REMOVES the key, so "absent" keeps meaning "never
    // annotated" and a shot restored to the board reads back exactly like one never touched.
    if (excluded) review[QStringLiteral("excluded")] = true;
    else          review.remove(QStringLiteral("excluded"));
    const QString ov = clubOverride.trimmed();
    if (!ov.isEmpty()) review[QStringLiteral("clubOverride")] = ov;
    else               review.remove(QStringLiteral("clubOverride"));

    if (review == before)
        return true;                    // nothing to say — and no 28 MB rewrite to say it
    // A block that held nothing but annotations, now cleared, goes away entirely rather than
    // lingering as an empty object a reader would have to interpret.
    if (review.isEmpty())
        root.remove(QStringLiteral("review"));
    else
        root[QStringLiteral("review")] = review;

    // The summary carries both annotations; saveDocument rebuilds it from this root.
    if (!saveDocument(swingDir, root, error))
        return false;
    clearJustWritten(swingDir);
    return true;
}

bool SwingDocWriter::updateLaunchMonitor(const QString &swingDir,
                                         const lm::LaunchMonitorReading &reading,
                                         QString *error,
                                         const QJsonObject &captureClub,
                                         bool *clubReplaced)
{
    if (clubReplaced)
        *clubReplaced = false;
    QJsonObject root = SwingStore::load(swingDir, error);
    if (root.isEmpty())
        return false;

    // The club the device named, replacing the one the swing was recorded under (see the header).
    // review.club is SEEDED by the live save with the session's club (writeSwingJson), so it is the
    // user's own pick only when it names neither the recorded club nor the device's: then nothing
    // changes. Otherwise both move to the device's club.
    if (!captureClub.isEmpty()) {
        QJsonObject review = root.value(QStringLiteral("review")).toObject();
        const QString reviewClub = review.value(QStringLiteral("club")).toString().trimmed();
        QJsonObject cap = root[QStringLiteral("capture")].toObject();
        const QString recorded = cap.value(QStringLiteral("club")).toObject()
                                    .value(QStringLiteral("name")).toString().trimmed();
        const QString named = captureClub.value(QStringLiteral("name")).toString().trimmed();
        const bool handSet = !reviewClub.isEmpty() && reviewClub != recorded && reviewClub != named;
        if (!handSet && !named.isEmpty() && named != recorded) {
            cap[QStringLiteral("club")] = captureClub;
            root[QStringLiteral("capture")] = cap;
            if (!reviewClub.isEmpty()) {
                review[QStringLiteral("club")] = named;
                root[QStringLiteral("review")] = review;
            }
            if (clubReplaced)
                *clubReplaced = true;
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
    if (!saveDocument(swingDir, root, error))
        return false;
    // This document just changed underneath any copy writeSwingJson() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    clearJustWritten(swingDir);
    return true;
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

bool SwingDocWriter::isDeviceOnlySwing(const QJsonObject &root)
{
    return root.value(QStringLiteral("streams")).toArray().isEmpty()
        && !root.value(QStringLiteral("launchMonitor")).toObject().isEmpty();
}

bool SwingDocWriter::refreshLaunchMonitorRows(const QString &swingDir, bool *changed, QString *error, bool dryRun)
{
    if (changed)
        *changed = false;
    QJsonObject root = SwingStore::load(swingDir, error);
    if (root.isEmpty())
        return false;
    const QJsonObject raw = root.value(QStringLiteral("launchMonitor")).toObject();
    if (raw.isEmpty() || !root.contains(QStringLiteral("analysis")))
        return true;

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
        return true;
    if (changed)
        *changed = true;
    if (dryRun)
        return true;

    an[QStringLiteral("metrics")] = metrics;
    lm::applyLmGates(an, raw);
    root[QStringLiteral("analysis")] = an;
    if (!saveDocument(swingDir, root, error))
        return false;
    clearJustWritten(swingDir);
    return true;
}

bool SwingDocWriter::writeDeviceOnlySwing(const QString &swingDir,
                                          const lm::LaunchMonitorReading &reading,
                                          const DeviceOnlyMeta &meta,
                                          QString *error)
{
    if (!reading.hasAnyValue()) {
        if (error) *error = QStringLiteral("refusing to write a shot from a reading with no values");
        return false;
    }

    const QDateTime when = meta.wallclockMs > 0
                               ? QDateTime::fromMSecsSinceEpoch(meta.wallclockMs, QTimeZone::UTC)
                               : QDateTime::currentDateTimeUtc();

    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/2");

    // t0_us is 0, not a buffer instant: there IS no buffer. Every t_us in this document
    // is therefore already window-relative, which is the domain every reader expects.
    root[QStringLiteral("clock")] = QJsonObject{
        { QStringLiteral("t0_us"),     0 },
        { QStringLiteral("wallclock"), when.toString(Qt::ISODateWithMs) },
    };
    // A zero-length window. Not a placeholder for a window we failed to capture — there
    // was nothing to capture, and a fabricated span would put a scrubber on a shot with
    // no frames behind it.
    root[QStringLiteral("window")] = QJsonObject{
        { QStringLiteral("startUs"), 0 },
        { QStringLiteral("endUs"),   0 },
    };
    root[QStringLiteral("swing")] = QJsonObject{
        { QStringLiteral("index"), meta.swingIndex },
        { QStringLiteral("id"),    meta.swingId },
    };
    root[QStringLiteral("session")] = QJsonObject{
        { QStringLiteral("id"), meta.sessionId },
    };
    root[QStringLiteral("athlete")] = QJsonObject{
        { QStringLiteral("uuid"), meta.athleteUuid },
        { QStringLiteral("name"), meta.athleteName },
    };
    root[QStringLiteral("capture")] = QJsonObject{
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
    // EMPTY, and that is the fact rather than a failure. hasVideo is derived from this,
    // so the picker and the carousel already read it correctly.
    root[QStringLiteral("streams")] = QJsonArray{};
    // No thumbnail key at all: an absent block is what summaryFromRoot reads as "no
    // thumbnail", where an empty one would point at a file that was never written.

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

    if (!QDir().mkpath(swingDir)) {
        if (error) *error = QStringLiteral("cannot create %1").arg(swingDir);
        return false;
    }

    if (!saveDocument(swingDir, root, error))
        return false;
    // This document just changed underneath any copy writeSwingJson() kept (takeJustWritten). Drop it:
    // a cached root older than its file would replay the wrong analysis, which is far worse than the
    // parse the cache saves.
    clearJustWritten(swingDir);
    return true;
}

SwingStore::ConvertResult SwingDocWriter::convertToPpsw(const QString &swingDir, bool deleteJson)
{
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
