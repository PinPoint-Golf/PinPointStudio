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

// swing_doc_reader.cpp — reading a swing document: SwingDocReader (readSwingJson,
// readLaunchMonitorDetail, streamOrigin, the session-list summary — readSwingSummary,
// writeSwingSummary and the JSON-era swing_summary.json sidecar — and the session folder
// listings), the public read helpers dataWarningDetailFrom, insertDtlTopOutOfView,
// overallScoreFromAnalysis and swingDeviceTokens, and the summary builders summaryFromRoot and
// summaryJson, which the writer also calls (swing_doc_internal.h). The writer is
// swing_doc_writer.cpp, the analysis-block serialisers swing_doc_analysis.cpp; the public API is
// swing_doc.h. Split out of swing_doc.cpp verbatim (refactor step 1.0, 14 Nov 2026).

#include "swing_doc.h"
#include "swing_doc_internal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

#include "swing_paths.h"
#include "swing_store.h"
#include "../LaunchMonitor/lm_device.h"             // connector vs device: launchMonitor.device
#include "../Analysis/swing_analysis.h"
#include "../Core/club_vocabulary.h"

namespace pinpoint {

// The helpers more than one swing_doc_*.cpp calls (swing_doc_internal.h).
using swingdoc_detail::serializeAnalysis;
using swingdoc_detail::summaryFromRoot;
using swingdoc_detail::summaryJson;

void insertDtlTopOutOfView(QVariantMap &detail, double handsFromTopPx, double clubPx)
{
    detail.insert(QStringLiteral("dtlTopOutOfView"),   true);
    detail.insert(QStringLiteral("dtlHandsFromTopPx"), int(std::lround(handsFromTopPx)));
    detail.insert(QStringLiteral("dtlClubPx"),         int(std::lround(clubPx)));
}

QVariantMap dataWarningDetailFrom(const QJsonObject &manifest)
{
    QVariantMap d;
    bool capture = false, imu = false;
    if (manifest.contains(QStringLiteral("captureIntegrity"))) {
        const QJsonObject ci = manifest[QStringLiteral("captureIntegrity")].toObject();
        const bool checked = ci.value(QStringLiteral("camerasChecked")).toInt() > 0;
        if (checked && !ci.value(QStringLiteral("ok")).toBool(true)) {
            capture = true;
            d.insert(QStringLiteral("holes"),       ci.value(QStringLiteral("holes")).toInt());
            d.insert(QStringLiteral("framesLost"),  ci.value(QStringLiteral("framesLost")).toInt());
            d.insert(QStringLiteral("worstHoleMs"), ci.value(QStringLiteral("worstHoleMs")).toDouble());
            d.insert(QStringLiteral("preImpact"),   ci.value(QStringLiteral("preImpact")).toBool());
            d.insert(QStringLiteral("postImpact"),  ci.value(QStringLiteral("postImpact")).toBool());
        }
        // A clip cut short of the capture window (issue #53). Only the facts that are true are
        // added, so a holes-only warning reads exactly as it always has. A late start or an early
        // end with frames before impact leaves `ok` true: its facts carry the ⚠ WITHOUT `capture`,
        // so — like clubRefused below — the shot stays in the session assessment. No frame before
        // impact fails the block (`capture` above) and withholds it.
        if (checked) {
            if (ci.value(QStringLiteral("startsAfterImpact")).toBool())
                d.insert(QStringLiteral("startsAfterImpact"), true);
            if (ci.value(QStringLiteral("lateStart")).toBool()) {
                d.insert(QStringLiteral("lateStart"),      true);
                d.insert(QStringLiteral("missingStartMs"), ci.value(QStringLiteral("missingStartMs")).toDouble());
            }
            if (ci.value(QStringLiteral("earlyEnd")).toBool()) {
                d.insert(QStringLiteral("earlyEnd"),     true);
                d.insert(QStringLiteral("missingEndMs"), ci.value(QStringLiteral("missingEndMs")).toDouble());
            }
        }
    }
    if (manifest.contains(QStringLiteral("imuIntegrity"))) {
        const QJsonObject ii = manifest[QStringLiteral("imuIntegrity")].toObject();
        if (ii.value(QStringLiteral("sourcesChecked")).toInt() > 0
            && !ii.value(QStringLiteral("refusionOk")).toBool(true)) {
            imu = true;
            d.insert(QStringLiteral("worstMaxDeg"), ii.value(QStringLiteral("worstMaxDeg")).toDouble());
        }
    }
    // A REFUSED club track (2026-10-01, analysis.club.refused): the tracker's own
    // witnesses contradicted it, so nothing from the face-on club is a measurement
    // on this shot. Unlike the two integrity blocks this does NOT withhold the
    // shot's body/wrist rows from the session assessment — the club rows are
    // simply absent (valid=false ⇒ every club-derived stage skipped) — so the
    // consumers that key the all-rows exclusion on `capture`/`imu` keep doing so.
    // The down-the-line camera could not see the top of the swing (2026-10-06,
    // analysis.clubDtl.summary.topOutOfView): the hands at P4 sat less than a club
    // length below the frame's top edge and nothing was measured there — 44–113 px
    // on a 988 px frame on 5 Oct 2026, every swing. A framing fact, not a broken
    // recording: like clubRefused it carries the ⚠ and never sets `capture`/`imu`,
    // so the shot stays in the session assessment.
    QString clubRefused;
    bool dtlTop = false;
    double dtlHandsFromTop = 0.0, dtlClub = 0.0;
    if (manifest.contains(QStringLiteral("analysis"))) {
        const QJsonObject an = manifest[QStringLiteral("analysis")].toObject();
        const QJsonObject club = an.value(QStringLiteral("club")).toObject();
        clubRefused = club.value(QStringLiteral("refused")).toString();
        const QJsonObject top = an.value(QStringLiteral("clubDtl")).toObject()
                                    .value(QStringLiteral("summary")).toObject()
                                    .value(QStringLiteral("topOutOfView")).toObject();
        if (top.value(QStringLiteral("flag")).toBool(false)) {
            dtlTop          = true;
            dtlHandsFromTop = top.value(QStringLiteral("handsFromTopPx")).toDouble();
            dtlClub         = top.value(QStringLiteral("clubPx")).toDouble();
        }
    }
    const bool videoShort = d.contains(QStringLiteral("lateStart")) || d.contains(QStringLiteral("earlyEnd"));
    if (!capture && !imu && clubRefused.isEmpty() && !dtlTop && !videoShort) return {};
    if (capture || imu) {
        d.insert(QStringLiteral("capture"), capture);
        d.insert(QStringLiteral("imu"),     imu);
    }
    if (!clubRefused.isEmpty()) d.insert(QStringLiteral("clubRefused"), clubRefused);
    if (dtlTop) insertDtlTopOutOfView(d, dtlHandsFromTop, dtlClub);
    return d;
}

// "score" is a bare int in /2 docs, a ScoreBreakdown object in /3+ (design §B.0a). See the header.
int overallScoreFromAnalysis(const QJsonObject &analysis)
{
    const QJsonValue scoreVal = analysis[QStringLiteral("score")];
    return scoreVal.isObject() ? scoreVal.toObject()[QStringLiteral("overall")].toInt()
                               : scoreVal.toInt();
}

namespace {


// ── the summary ─────────────────────────────────────────────────────────────
//
// The document is the source of truth, but it is never fully read to build a session-list row:
// documents run to tens of MB (analysis.pose2d alone is ~13 MB on a Wrist swing, retained for
// replay overlays) while the picker needs a handful of scalars from each.
//
// swing.ppsw carries those scalars INSIDE the document, as the top-level `summary` block, written
// by every writer from the root it is writing (saveDocument below). It sits in the file's root
// chunk, so a list read decodes a few KB and never touches the pose chunks — and because it is
// written in the same atomic rename as everything else, it cannot be stale and needs no guard.
//
// A JSON-era swing (swing.json, read indefinitely) keeps the old arrangement: swing_summary.json
// caches the same fields, guarded by the source document's size+mtime so any out-of-band rewrite is
// detected and the sidecar regenerated. Pure cache: always safe to delete, always regenerable. The
// first rewrite of such a swing makes it a swing.ppsw and removes both files (SwingStore::save).

// /2: club is resolved through swingDocClub() — review.club, else capture.club.name, else
// the stub. A /1 sidecar cached the review-or-stub answer, and its size+mtime guard still
// matches (the fix changed no swing.json), so bumping the schema is the ONLY thing that
// retires the stale "DRIVER" it holds for every camera swing that was never edited.
// /4: + metrics, rating, note, lmDeviceKind, dataWarningDetail — everything a carousel row shows, so
// loading a session never has to fat-parse (2026-09-16). Pure cache: an older sidecar simply fails the
// schema check and is rewritten from the document.
// `devices` and `handedness` are ADDITIVE to /4, not a bump: a bump would fat-parse every swing.ppsw on
// the next list (the stall /4 exists to prevent). Each reader makes up for an older block without them.
constexpr auto kSummarySchema = "pinpoint.swingsummary/4";

QString summaryPath(const QString &swingDir) { return SwingStore::legacySummaryPath(swingDir); }
QString sourcePath (const QString &swingDir) { return SwingStore::jsonPath(swingDir); }

// The carousel's metric chips: each metric's value at Impact, formatted in its own unit.
//
// ⚠ ONE IMPLEMENTATION, called by the full reader AND the summary (2026-09-16). The chips are what a
// row shows whether it was rebuilt from the document or from the sidecar, and two copies of this
// formatting would drift into two different-looking cards for the same swing.
QVariantMap impactMetricsFromAnalysis(const QJsonObject &an)
{
    QVariantMap metrics;
    for (const QJsonValue &mv : an[QStringLiteral("metrics")].toArray()) {
        const QJsonObject m = mv.toObject();
        bool found = false;
        double impact = 0.0;
        for (const QJsonValue &sv : m[QStringLiteral("phaseSamples")].toArray()) {
            const QJsonObject s = sv.toObject();
            if (s[QStringLiteral("phase")].toInt() == int(analysis::Phase::Impact)) {
                impact = s[QStringLiteral("value")].toDouble(); found = true; break;
            }
        }
        if (!found)
            continue;
        // Format against the metric's OWN unit. This loop sees every detail series, not just the
        // signed-degree wrist ones, so hardcoding "°" rendered a ×frame heel lift or an mph clubhead
        // speed as "+0°". Degrees keep their exact previous formatting (signed, rounded) so the wrist
        // metrics that drive the carousel are unchanged; everything else reads in the unit it was
        // actually measured in.
        const QString unit = m[QStringLiteral("unit")].toString();
        QString val;
        if (unit == QStringLiteral("°")) {
            const long r = std::lround(impact);
            val = (r > 0 ? QStringLiteral("+") : QString()) + QString::number(r) + unit;
        } else {
            // 2 sf past the point for small magnitudes, whole numbers for large ones (mm, ms) — and no
            // forced "+", which is meaningless for a ratio, a percentage or a speed.
            val = QString::number(impact, 'f', std::abs(impact) < 100.0 ? 2 : 0) + unit;
        }
        metrics.insert(m[QStringLiteral("key")].toString(),
                       QVariantMap{ { QStringLiteral("label"), m[QStringLiteral("label")].toString() },
                                    { QStringLiteral("value"), val } });
    }
    return metrics;
}

} // namespace

namespace swingdoc_detail {

// THE extractor for the session-picker scalars. readSwingJson() and the sidecar both go
// through this, so the cheap path can never disagree with the full path about a score
// shape, a club fallback or a thumbnail name.
SwingSummary summaryFromRoot(const QJsonObject &root, const QString &swingDir)
{
    SwingSummary s;
    s.swingDir = swingDir;
    if (root.isEmpty())
        return s;

    s.ordinal = root[QStringLiteral("swing")].toObject()[QStringLiteral("index")].toInt();
    // review.club, else the club selected at capture, else the stub — see swingDocClub().
    s.club    = swingDocClub(root);

    const QString wc = root[QStringLiteral("clock")].toObject()[QStringLiteral("wallclock")].toString();
    const QDateTime dt = QDateTime::fromString(wc, Qt::ISODateWithMs);
    s.timestampLabel = dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("hh:mm:ss")) : wc;
    s.wallclockMs    = dt.isValid() ? dt.toMSecsSinceEpoch() : 0;

    // ⚠ A VIDEO STREAM WITH NO `file` IS NOT VIDEO.  A phone clip that has been
    // asked for and has not arrived is recorded as a `kind: video` element
    // carrying only its `origin` (see updateStreamOrigin), precisely so the swing
    // remembers a clip is owed — and answering `hasVideo` for it would have the
    // carousel offer a replay of bytes that are not there.  Every element the
    // exporter writes for a real encode carries `file`, so this narrows nothing
    // that exists today.  DiskReplaySource and SwingDiskLoader already skip an
    // element whose file is missing; this makes the summary agree with them.
    for (const QJsonValue &v : root[QStringLiteral("streams")].toArray()) {
        const QJsonObject el = v.toObject();
        if (el[QStringLiteral("kind")].toString() != QLatin1String("video")) continue;
        if (el[QStringLiteral("file")].toString().isEmpty()) continue;
        s.hasVideo = true;
        break;
    }

    const QJsonObject thumb = root[QStringLiteral("thumbnail")].toObject();
    if (!thumb.isEmpty())
        s.thumbnailPath = swingDir + QStringLiteral("/")
                        + thumb[QStringLiteral("file")].toString(QStringLiteral("thumb.jpg"));

    if (root.contains(QStringLiteral("analysis"))) {
        const QJsonObject an = root[QStringLiteral("analysis")].toObject();
        s.score = overallScoreFromAnalysis(an);
        // The chips, from the same helper the full reader uses. Reads only metrics[] — the pose track
        // this summary exists to avoid is never touched.
        s.metrics = impactMetricsFromAnalysis(an);
    }
    // The data warning is read from the same two blocks the full reader uses, so the
    // ledger's cheap path and the carousel's fat path can never disagree about it.
    s.dataWarningDetail = dataWarningDetailFrom(root);
    s.dataWarning       = !s.dataWarningDetail.isEmpty();

    // User review (stars, note) — the same block updateReview() writes through.
    if (root.contains(QStringLiteral("review"))) {
        const QJsonObject rv = root[QStringLiteral("review")].toObject();
        s.rating = std::clamp(rv[QStringLiteral("rating")].toInt(), 0, 5);
        s.note   = rv[QStringLiteral("note")].toString();
        // The launch monitor board's annotations — optional keys, absent = not annotated.
        s.excluded     = rv[QStringLiteral("excluded")].toBool(false);
        s.clubOverride = rv[QStringLiteral("clubOverride")].toString();
    }
    // Which connector carried it, and which device measured it (lm_device.h), for the row's caption.
    s.lmDeviceKind = root[QStringLiteral("launchMonitor")].toObject()
                         [QStringLiteral("kind")].toString();
    s.lmDevice     = lm::deviceKeyOfBlock(root[QStringLiteral("launchMonitor")].toObject());
    s.devices      = swingDeviceTokens(root);
    s.handedness   = root[QStringLiteral("athlete")].toObject()
                         [QStringLiteral("handedness")].toString();

    s.ok = true;
    return s;
}

// The summary fields, as JSON — the `summary` block of a swing.ppsw, and (plus its guard) the body
// of a JSON-era swing_summary.json. One spelling for both, so the two cheap paths cannot drift.
QJsonObject summaryJson(const SwingSummary &s)
{
    // Thumbnails are stored relative so a moved or renamed library still resolves.
    const QString thumbFile = s.thumbnailPath.isEmpty()
                                  ? QString()
                                  : QFileInfo(s.thumbnailPath).fileName();

    return QJsonObject{
        { QStringLiteral("schema"), QString::fromLatin1(kSummarySchema) },
        { QStringLiteral("ordinal"),        s.ordinal },
        { QStringLiteral("timestampLabel"), s.timestampLabel },
        { QStringLiteral("wallclockMs"),    double(s.wallclockMs) },
        { QStringLiteral("club"),           s.club },
        { QStringLiteral("hasVideo"),       s.hasVideo },
        { QStringLiteral("thumbnailFile"),  thumbFile },
        { QStringLiteral("score"),          s.score },
        { QStringLiteral("dataWarning"),    s.dataWarning },
        // Schema /4 — the rest of what a carousel row shows, so a session load never fat-parses.
        // Written even when empty: absent and empty mean the same thing to the reader, and a row
        // rebuilt from this must look exactly like one rebuilt from the document.
        { QStringLiteral("metrics"),          QJsonObject::fromVariantMap(s.metrics) },
        { QStringLiteral("rating"),           s.rating },
        { QStringLiteral("note"),             s.note },
        { QStringLiteral("lmDeviceKind"),     s.lmDeviceKind },
        // The DEVICE beside the connector (lm_device.h). Additive to /4: an older summary lacking it
        // is right as it stands, because the reader derives it from lmDeviceKind exactly as the
        // document would — no device was recorded before this key existed.
        { QStringLiteral("lmDevice"),         s.lmDevice },
        { QStringLiteral("dataWarningDetail"),QJsonObject::fromVariantMap(s.dataWarningDetail) },
        // The launch monitor board's annotations — additive to /4, read with defaults (see
        // SwingSummary::excluded for why this is not a schema bump).
        { QStringLiteral("excluded"),         s.excluded },
        { QStringLiteral("clubOverride"),     s.clubOverride },
        // Additive to /4 as well, but an older block lacking it is NOT right — see
        // SwingSummary::devices for how each reader makes up for that.
        { QStringLiteral("devices"),          QJsonArray::fromStringList(s.devices) },
        // The same again (SwingSummary::handedness). Written even when empty: the key's presence is
        // what says this summary was written knowing about it.
        { QStringLiteral("handedness"),       s.handedness },
    };
}

} // namespace swingdoc_detail

namespace {

SwingSummary summaryFromShot(const PersistedShot &ps)
{
    SwingSummary s;
    s.ok             = ps.ok;
    s.swingDir       = ps.swingDir;
    s.ordinal        = ps.ordinal;
    s.timestampLabel = ps.timestampLabel;
    s.wallclockMs    = ps.wallclockMs;
    s.club           = ps.club;
    s.hasVideo       = ps.hasVideo;
    s.thumbnailPath  = ps.thumbnailPath;
    s.score          = ps.score;
    s.dataWarning    = ps.dataWarning;
    // The row fields (schema /4). The shot has already been parsed here, so these are copies rather
    // than work — and without them a sidecar written on the live path would send the next session load
    // back to the full parse this exists to avoid.
    s.metrics           = ps.metrics;
    s.rating            = ps.rating;
    s.note              = ps.note;
    s.lmDeviceKind      = ps.lmDeviceKind;
    s.lmDevice          = ps.lmDevice;
    s.devices           = ps.devices;
    s.handedness        = ps.handedness;
    s.dataWarningDetail = ps.dataWarningDetail;
    s.excluded          = ps.excluded;
    s.clubOverride      = ps.clubOverride;
    return s;
}

bool writeSummaryFile(const SwingSummary &s, QString *error)
{
    if (!s.ok || s.swingDir.isEmpty()) {
        if (error) *error = QStringLiteral("refusing to index a swing that did not parse");
        return false;
    }
    const QFileInfo src(sourcePath(s.swingDir));
    if (!src.exists()) {
        if (error) *error = QStringLiteral("no swing.json in %1").arg(s.swingDir);
        return false;
    }

    QJsonObject root = summaryJson(s);
    root.insert(QStringLiteral("source"), QJsonObject{
        { QStringLiteral("size"),     double(src.size()) },
        { QStringLiteral("mtime_ms"), double(src.lastModified().toMSecsSinceEpoch()) } });

    const QString path = summaryPath(s.swingDir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("failed to commit %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// Fill a summary from a summary object (the block or a sidecar). Everything but the guard.
void summaryFromJson(const QJsonObject &root, const QString &swingDir, SwingSummary &s)
{
    s.ordinal     = root[QStringLiteral("ordinal")].toInt();
    s.wallclockMs = qint64(root[QStringLiteral("wallclockMs")].toDouble());
    // Re-derive the label rather than trusting the cached one: it is local-time
    // formatted, so a library carried across timezones would otherwise show the
    // times of wherever it was indexed. wallclockMs is absolute and is not.
    s.timestampLabel =
        s.wallclockMs != 0
            ? QDateTime::fromMSecsSinceEpoch(s.wallclockMs).toLocalTime()
                  .toString(QStringLiteral("hh:mm:ss"))
            : root[QStringLiteral("timestampLabel")].toString();
    s.club           = root[QStringLiteral("club")].toString();
    s.hasVideo       = root[QStringLiteral("hasVideo")].toBool();
    const QString tf = root[QStringLiteral("thumbnailFile")].toString();
    s.thumbnailPath  = tf.isEmpty() ? QString() : swingDir + QStringLiteral("/") + tf;
    s.score          = root[QStringLiteral("score")].toInt();
    s.dataWarning    = root[QStringLiteral("dataWarning")].toBool(false);
    // Schema /4 row fields. An older summary never reaches here — the caller's schema check
    // rejects it — so these are always present, and an empty object is a real empty.
    s.metrics           = root[QStringLiteral("metrics")].toObject().toVariantMap();
    s.rating            = std::clamp(root[QStringLiteral("rating")].toInt(), 0, 5);
    s.note              = root[QStringLiteral("note")].toString();
    s.lmDeviceKind      = root[QStringLiteral("lmDeviceKind")].toString();
    s.lmDevice          = lm::canonicalDeviceKey(root.contains(QStringLiteral("lmDevice"))
                                                     ? root[QStringLiteral("lmDevice")].toString()
                                                     : s.lmDeviceKind);
    s.dataWarningDetail = root[QStringLiteral("dataWarningDetail")].toObject().toVariantMap();
    // Additive to /4: a summary written before these existed lacks them, and is right to.
    s.excluded          = root[QStringLiteral("excluded")].toBool(false);
    s.clubOverride      = root[QStringLiteral("clubOverride")].toString();
    for (const QJsonValue &d : root[QStringLiteral("devices")].toArray())
        s.devices.append(d.toString());
    s.handedness = root[QStringLiteral("handedness")].toString();
    s.fromSidecar    = true;
    s.ok             = true;
}

SwingDocWriter::StreamOrigin originFromJson(const QJsonObject &j)
{
    SwingDocWriter::StreamOrigin o;
    o.transport    = j.value(QStringLiteral("transport")).toString();
    o.peerId       = j.value(QStringLiteral("peerId")).toString();
    o.sessionId    = j.value(QStringLiteral("sessionId")).toString();
    o.captureId    = j.value(QStringLiteral("captureId")).toString();
    o.streamId     = j.value(QStringLiteral("streamId")).toString();
    o.transfer     = j.value(QStringLiteral("transfer")).toString();
    o.completeness = j.value(QStringLiteral("completeness")).toString();
    o.absentReason = j.value(QStringLiteral("reason")).toString();
    o.committedAt  = j.value(QStringLiteral("committedAt")).toString();
    return o;
}

} // namespace

SwingDocWriter::StreamOrigin SwingDocReader::streamOrigin(const QString &swingDir,
                                                          const QString &alias)
{
    const QJsonObject root = SwingStore::load(swingDir);
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject el = v.toObject();
        if (el.value(QStringLiteral("alias")).toString() != alias) continue;
        if (!el.contains(QStringLiteral("origin"))) return {};
        return originFromJson(el.value(QStringLiteral("origin")).toObject());
    }
    return {};
}

// ── reader ──────────────────────────────────────────────────────────────────

QVariantMap SwingDocReader::readLaunchMonitorDetail(const QString &swingDir)
{
    const QJsonObject an = SwingStore::loadAnalysis(swingDir)
                               .value(QStringLiteral("analysis")).toObject();
    if (an.isEmpty())
        return {};
    // The entries verbatim, as readSwingJson() converts them, so the board reads one shape
    // whichever read filled the row. Converting only these is the saving: the other metric
    // curves are most of the analysis and the board never looks at them.
    QVariantList series;
    for (const QJsonValue &v : an[QStringLiteral("metrics")].toArray()) {
        const QJsonObject m = v.toObject();
        const QString key = m.value(QStringLiteral("key")).toString();
        if (key.startsWith(QLatin1String("lm.")) || key == QLatin1String("lowPointAhead"))
            series.append(m.toVariantMap());
    }
    QVariantMap detail{ { QStringLiteral("series"), series } };
    // All of them, not just the `lm.` ones: they are a few records, and trust::gateFor decides
    // which apply to a reading exactly as it does for a fully read row.
    if (an.contains(QStringLiteral("gatesVersion"))) {
        detail.insert(QStringLiteral("gatesVersion"), an[QStringLiteral("gatesVersion")].toInt());
        detail.insert(QStringLiteral("gates"), an[QStringLiteral("gates")].toArray().toVariantList());
    }
    return detail;
}

PersistedShot SwingDocReader::readSwingJson(const QString &swingDir)
{
    PersistedShot ps;
    ps.swingDir = swingDir;

    const QJsonObject root = SwingStore::load(swingDir);
    if (root.isEmpty())
        return ps;

    // Shared scalars (ordinal, timestamp, video presence, thumbnail, score, club).
    // Note: club here already reflects the review block, which the tail of this
    // function re-applies harmlessly alongside rating/note.
    const SwingSummary sum = summaryFromRoot(root, swingDir);
    ps.ordinal        = sum.ordinal;
    ps.club           = sum.club;
    ps.timestampLabel = sum.timestampLabel;
    ps.wallclockMs    = sum.wallclockMs;
    ps.hasVideo       = sum.hasVideo;
    ps.thumbnailPath  = sum.thumbnailPath;
    ps.score          = sum.score;

    // WHO swung it. Read here and NOT into SwingSummary: the summary is the session-picker row and
    // it grades nothing, so a field on it would ride in the sidecar with no reader — and an old
    // sidecar would carry a blank one until swing.json happened to change. Both are absent on a
    // document written before the exporter recorded them, which reads as "unknown athlete" and
    // resolves the universal corridor.
    const QJsonObject athlete = root[QStringLiteral("athlete")].toObject();
    ps.athleteUuid    = athlete[QStringLiteral("uuid")].toString();
    ps.athleteName    = athlete[QStringLiteral("name")].toString();
    ps.handedness     = athlete[QStringLiteral("handedness")].toString();

    // The kind alone out of the raw block, for the same reason as the athlete above.
    // Everything else in that block (deviceShotId, sourcePath, the raw columns) is
    // provenance for tooling and has no reader here — the readings are already in the metrics.
    ps.lmDeviceKind   = root[QStringLiteral("launchMonitor")].toObject()
                            .value(QStringLiteral("kind")).toString();
    ps.lmDevice       = lm::deviceKeyOfBlock(root[QStringLiteral("launchMonitor")].toObject());
    ps.devices        = swingDeviceTokens(root);

    // Only video presence is reconstructed here. imu / pose streams and the raw
    // sidecar are not parsed on reload yet — see the "Reload & replay consumer
    // contract" in docs/developer/swing_export_developer_guide.md for the shapes a future
    // consumer must honor (IMU json/csv/binary, pose coco17, raw reconstruction).

    if (root.contains(QStringLiteral("analysis"))) {
        const QJsonObject an = root[QStringLiteral("analysis")].toObject();
        const QJsonValue scoreVal = an[QStringLiteral("score")];
        const QJsonObject scoreObj = scoreVal.toObject();   // empty if /2 (a number)
        const int overall = overallScoreFromAnalysis(an);

        // analysisDetail in the live QML-role shape (metrics→series, score→overall).
        ps.analysisDetail = QVariantMap{
            { QStringLiteral("tier"),    an[QStringLiteral("tier")].toInt() },
            { QStringLiteral("overall"), overall },
            { QStringLiteral("series"),  an[QStringLiteral("metrics")].toArray().toVariantList() },
            { QStringLiteral("phases"),  an[QStringLiteral("phases")].toArray().toVariantList() },
        };
        // Resemblance estimand + uncertainty interval (/3): surfaced as sibling keys so
        // QML reads detail.pattern / .resemblance / .interval directly. Absent for /2 or
        // for adherence (Swing/GRF) scores.
        if (scoreVal.isObject()) {
            if (scoreObj.contains(QStringLiteral("pattern")))
                ps.analysisDetail.insert(QStringLiteral("pattern"),
                                         scoreObj[QStringLiteral("pattern")].toString());
            if (scoreObj.contains(QStringLiteral("blended")))
                ps.analysisDetail.insert(QStringLiteral("blended"),
                                         scoreObj[QStringLiteral("blended")].toBool());
            if (scoreObj.contains(QStringLiteral("resemblance")))
                ps.analysisDetail.insert(QStringLiteral("resemblance"),
                                         scoreObj[QStringLiteral("resemblance")].toObject().toVariantMap());
            if (scoreObj.contains(QStringLiteral("interval")))
                ps.analysisDetail.insert(QStringLiteral("interval"),
                                         scoreObj[QStringLiteral("interval")].toObject().toVariantMap());
        }
        // Adherence contribution maps — surfaced as sibling keys (detail.perRegion /
        // .perPhase) for the Verdict donut breakdown. Absent for /2 docs, for
        // resemblance scores, and for any doc written before serializeScore emitted
        // them; the donut simply renders without segments then.
        if (scoreVal.isObject()) {
            if (scoreObj.contains(QStringLiteral("perRegion")))
                ps.analysisDetail.insert(QStringLiteral("perRegion"),
                                         scoreObj[QStringLiteral("perRegion")].toObject().toVariantMap());
            if (scoreObj.contains(QStringLiteral("perPhase")))
                ps.analysisDetail.insert(QStringLiteral("perPhase"),
                                         scoreObj[QStringLiteral("perPhase")].toObject().toVariantMap());
        }
        // Additive ShaftTracker + segmentation blocks — same variant shapes as
        // the live toAnalysisDetail (shot_processor.cpp); absent in older files
        // (missing segmentation = full-window bounds).
        if (an.contains(QStringLiteral("pose2d")))
            ps.analysisDetail.insert(QStringLiteral("pose2d"),
                                     an[QStringLiteral("pose2d")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("club")))
            ps.analysisDetail.insert(QStringLiteral("club"),
                                     an[QStringLiteral("club")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("ball")))
            ps.analysisDetail.insert(QStringLiteral("ball"),
                                     an[QStringLiteral("ball")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("impact")))
            ps.analysisDetail.insert(QStringLiteral("impact"),
                                     an[QStringLiteral("impact")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("segmentation")))
            ps.analysisDetail.insert(QStringLiteral("segmentation"),
                                     an[QStringLiteral("segmentation")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("bindings")))
            ps.analysisDetail.insert(QStringLiteral("bindings"),
                                     an[QStringLiteral("bindings")].toArray().toVariantList());
        if (an.contains(QStringLiteral("timings")))
            ps.analysisDetail.insert(QStringLiteral("timings"),
                                     an[QStringLiteral("timings")].toObject().toVariantMap());
        // the gate records (trust_and_gates_design.md §6), as the replay source carries them
        if (an.contains(QStringLiteral("gatesVersion"))) {
            ps.analysisDetail.insert(QStringLiteral("gatesVersion"), an[QStringLiteral("gatesVersion")].toInt());
            ps.analysisDetail.insert(QStringLiteral("gates"), an[QStringLiteral("gates")].toArray().toVariantList());
        }

        // Flat metrics: each metric's value at Impact, in its own unit. Shared with the summary
        // sidecar so a row rebuilt either way shows identical chips.
        ps.metrics = impactMetricsFromAnalysis(an);
    }

    // User review (rating/note/club) — written through by updateReview after edits.
    if (root.contains(QStringLiteral("review"))) {
        const QJsonObject rv = root[QStringLiteral("review")].toObject();
        ps.rating = std::clamp(rv[QStringLiteral("rating")].toInt(), 0, 5);
        ps.note   = rv[QStringLiteral("note")].toString();
        // Older review blocks predate club; keep the stub default when absent/empty.
        const QString club = rv[QStringLiteral("club")].toString();
        if (!club.isEmpty())
            ps.club = club;
        // The launch monitor board's annotations (PersistedShot::excluded): optional keys,
        // and absent is the honest default for every document that predates them.
        ps.excluded     = rv[QStringLiteral("excluded")].toBool(false);
        ps.clubOverride = rv[QStringLiteral("clubOverride")].toString();
    }

    // Data-integrity verdicts (additive top-level blocks: imuIntegrity from the
    // re-fusion parity check, captureIntegrity from the frame-timestamp check).
    // Legacy swings lack both → no warning.
    ps.dataWarningDetail = dataWarningDetailFrom(root);
    ps.dataWarning       = !ps.dataWarningDetail.isEmpty();

    ps.ok = true;
    return ps;
}

QStringList swingDeviceTokens(const QJsonObject &root)
{
    bool faceOn = false, dtl = false, impact = false, camera = false, hackmotion = false, imu = false;
    for (const QJsonValue &v : root[QStringLiteral("streams")].toArray()) {
        const QJsonObject el   = v.toObject();
        const QString     kind = el[QStringLiteral("kind")].toString();
        if (kind == QLatin1String("video")) {
            if (el[QStringLiteral("file")].toString().isEmpty()) continue;   // owed, not recorded
            const QString p = el[QStringLiteral("setup")].toObject()
                                [QStringLiteral("perspectiveName")].toString();
            if      (p == QLatin1String("FaceOn"))      faceOn = true;
            else if (p == QLatin1String("DownTheLine")) dtl    = true;
            else if (p == QLatin1String("Impact"))      impact = true;
            else                                        camera = true;
        } else if (kind == QLatin1String("imu")) {
            if (el[QStringLiteral("instrument")].toString() == QLatin1String("hackmotion"))
                hackmotion = true;
            else
                imu = true;
        }
    }
    QStringList out;
    if (faceOn)     out << QStringLiteral("faceOn");
    if (dtl)        out << QStringLiteral("dtl");
    if (impact)     out << QStringLiteral("impact");
    if (camera)     out << QStringLiteral("camera");
    if (hackmotion) out << QStringLiteral("hackmotion");
    if (imu)        out << QStringLiteral("imu");
    // The DEVICE, not the connector: a GC Quad read through FSX2020 and one read through Premier
    // are one tag and one filter chip (lm_device.h).
    const QString lmDevice = lm::deviceKeyOfBlock(root[QStringLiteral("launchMonitor")].toObject());
    if (!lmDevice.isEmpty())
        out << QStringLiteral("lm:") + lmDevice;
    return out;
}

bool SwingDocReader::writeSwingSummary(const PersistedShot &shot, QString *error)
{
    // A swing.ppsw carries its summary inside, written with the document — there is nothing to
    // index. Only a JSON-era swing has a sidecar.
    if (SwingStore::info(shot.swingDir).format == SwingStore::Format::Ppsw)
        return true;
    return writeSummaryFile(summaryFromShot(shot), error);
}

SwingSummary SwingDocReader::readSwingSummary(const QString &swingDir, bool writeSidecar,
                                              SwingStore::LazyDocument *parsed)
{
    SwingSummary s;
    s.swingDir = swingDir;

    const SwingStore::DocInfo doc = SwingStore::info(swingDir);
    if (!doc.exists())
        return s;                       // no document at all — nothing to summarise

    if (doc.format == SwingStore::Format::Ppsw) {
        // Fast path: the document's own summary block, from its root chunk alone.
        QJsonObject heads;
        const QJsonObject block = SwingStore::loadSummaryBlock(swingDir, &heads);
        if (block[QStringLiteral("schema")].toString() == QLatin1String(kSummarySchema)) {
            summaryFromJson(block, swingDir, s);
            // A block written before `devices` existed: the stream heads came out of the same root
            // chunk read, so derive them here rather than send the whole library to a fat parse.
            if (!block.contains(QStringLiteral("devices")))
                s.devices = swingDeviceTokens(heads);
            // ...and the handedness, out of the same read, for a block written before it existed.
            if (!block.contains(QStringLiteral("handedness")))
                s.handedness = heads[QStringLiteral("athlete")].toObject()
                                   [QStringLiteral("handedness")].toString();
            return s;
        }
        // Missing or an older schema: derive it from the document. Not written back — a list read
        // must never rewrite a document another writer may be about to rewrite; the next real write
        // refreshes the block.
        if (!writeSidecar)
            return s;
        return summaryFromRoot(parsed ? parsed->root() : SwingStore::load(swingDir), swingDir);
    }

    // ── A JSON-era swing: the swing_summary.json sidecar ────────────────────────────────────────
    const QFileInfo src(doc.path);

    // Fast path: a sidecar whose guard still matches the source document.
    QFile f(summaryPath(swingDir));
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError pe;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
        f.close();
        const QJsonObject srcObj = root[QStringLiteral("source")].toObject();
        const bool fresh =
            pe.error == QJsonParseError::NoError
            && root[QStringLiteral("schema")].toString() == QLatin1String(kSummarySchema)
            // A sidecar from before `devices` cannot say what recorded the swing — stale.
            && root.contains(QStringLiteral("devices"))
            && root.contains(QStringLiteral("handedness"))
            // The one stamp rule (SwingStore::sameDocumentStamp): a copy that kept the document's
            // mtime to the whole second is the same document.
            && SwingStore::sameDocumentStamp(qint64(srcObj[QStringLiteral("size")].toDouble()),
                                             qint64(srcObj[QStringLiteral("mtime_ms")].toDouble()),
                                             src.size(), src.lastModified().toMSecsSinceEpoch());
        if (fresh) {
            summaryFromJson(root, swingDir, s);
            return s;
        }
    }

    // Miss or stale. Callers that must not block say so; they get ok=false and render the
    // swing without detail until something that is allowed to fat-parse indexes it.
    if (!writeSidecar)
        return s;

    // Parse the source document directly rather than going through readSwingJson(): this
    // skips building analysisDetail, whose pose2d keypoint track is the bulk of the cost.
    // The shared parse when the caller has one (the phase-grid reader may already have paid for it).
    SwingStore::LazyDocument own(swingDir);
    SwingStore::LazyDocument &shared = parsed ? *parsed : own;
    const QJsonObject &root = shared.root();
    if (root.isEmpty())
        return s;

    s = summaryFromRoot(root, swingDir);
    // The sidecar's guard is the file's size and mtime AT WRITE TIME (writeSummaryFile). A rewrite
    // since the parse — after another reader paid for it, or during this one's own parse — would
    // index old content under the new document's guard, so the summary is returned but not indexed.
    if (s.ok && shared.stillCurrent())
        writeSummaryFile(s, nullptr);   // best-effort; a read-only library just stays slow
    return s;
}

QStringList SwingDocReader::findSwingDirs(const QString &sessionDir)
{
    QDir d(sessionDir);
    QStringList out;
    for (const QString &name : d.entryList(QStringList{ QStringLiteral("swing_*") },
                                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        out.append(sessionDir + QStringLiteral("/") + name);
    return out;
}

QString SwingDocReader::latestSessionDir(const QString &libraryRoot, const QString &athleteName)
{
    if (libraryRoot.isEmpty() || athleteName.isEmpty())
        return {};
    const QString athleteDir = libraryRoot + QStringLiteral("/") + SwingPaths::sanitise(athleteName);
    const QFileInfoList sessions =
        QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (sessions.isEmpty())
        return {};
    // Pick the most recently modified session dir. Folder names now embed the
    // naming pattern's tokens (date / athlete / session-type), so a plain name
    // sort no longer tracks recency when several session types share one day.
    const QFileInfo *newest = &sessions.first();
    for (const QFileInfo &fi : sessions)
        if (fi.lastModified() > newest->lastModified())
            newest = &fi;
    return newest->absoluteFilePath();
}

QStringList SwingDocReader::sessionDirs(const QString &libraryRoot, const QString &athleteName)
{
    if (libraryRoot.isEmpty() || athleteName.isEmpty())
        return {};
    const QString athleteDir = libraryRoot + QStringLiteral("/") + SwingPaths::sanitise(athleteName);
    QFileInfoList sessions =
        QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    // Most-recently-modified first — same recency basis as latestSessionDir(),
    // since the naming pattern means a plain name sort doesn't track recency.
    std::sort(sessions.begin(), sessions.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    QStringList out;
    out.reserve(sessions.size());
    for (const QFileInfo &fi : sessions)
        out.append(fi.absoluteFilePath());
    return out;
}

} // namespace pinpoint
