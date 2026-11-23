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

#pragma once

// The swing document's public API. Its code is in three files, split by job (14 Nov 2026):
//   swing_doc_writer.cpp    SwingDocWriter; the imuIntegrity / captureIntegrity blocks
//                           (imuIntegrityJson, applyImuIntegrity, captureIntegrityJson,
//                           applyCaptureIntegrity); the launch monitor blocks and derived rows
//   swing_doc_reader.cpp    SwingDocReader; dataWarningDetailFrom, overallScoreFromAnalysis,
//                           insertDtlTopOutOfView, swingDeviceTokens; the summary and its sidecar
//   swing_doc_analysis.cpp  the `analysis` block, composed from the per-block serialisers beside
//                           their producers (step 1.3a): serializeAnalysis (internal),
//                           analysisFromDocument / analysisReadOriginUs, the forwarder
//                           poseTrackToJson, and analysisBlockFor (internal)
//   (and the reader)        the one projection of the block for QML (step 1.3b):
//                           analysisDetailFor, analysisDetailForDocument, liveAnalysisDetail
// swing_doc_internal.h declares the few helpers more than one of them calls.

#include "launch_monitor_reading.h"
#include "swing_store.h"
#include "../Core/sidecar_policy.h"   // SidecarPolicy (refactor 3.7-0)
#include "../DomainModel/recording_repository.h"   // WriteResult, ReviewEdit, StreamArrival, DeviceOnlyMeta

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace pinpoint::analysis { struct SwingAnalysis; struct ImpactTrack2D; struct PoseTrack2D; }

namespace pinpoint {

struct ImuRefusionVerdict;   // IMU/imu_refusion_check.h — by pointer, so this
                             // header need not drag the filter/refuser chain in.
struct CaptureIntegrityVerdict;   // Analysis/capture_integrity_check.h — same reason.
class  SwingStoreRecordingRepository;   // swing_store_repository.h — the one writer (friend below)

// ── The swing.json "imuIntegrity" block ──────────────────────────────────────
//
// Two producers write this block — the live capture join (ShotProcessor) and the
// re-analysis write-back — so it is spelled ONCE here rather than inline at each.
// The block's fields are subtle enough that two hand-rolled copies would drift:
// `filter` describes the sources that WERE checked, not every IMU lane in the
// window, and `sourcesChecked` is the field that says how many that was.
QJsonObject imuIntegrityJson(const ImuRefusionVerdict &v);

// A pose track in the `analysis.pose2d` shape: { camera, keypointCount, frames,
// [decode], [cropRect], [smoothed], [adaptFallbacks], [synth] }. ONE builder for
// the face-on `pose2d` and the down-the-line `poseDtl` blocks, so every reader
// parses one shape: it forwards to the block's serialiser,
// poseTrackToAnalysisJson (Pose/pose_track_json.h). Times go through the one rule,
// windowTimeUs (absolute ≥ windowT0 made relative, relative passes through; 0 keeps the
// live domain). The caller skips an empty track — this writes what it is given.
QJsonObject poseTrackToJson(const analysis::PoseTrack2D &t, qint64 windowT0);

// ── One time base (refactor step 1.3a; information_model.md §7, IM-2) ────────────────────────
// A swing document's analysis is window time and says so: `analysis.timeBase: "window"`.
// analysisReadOriginUs is THE ONE PLACE a document without the key (written before step 1.3a)
// is guessed: 0 for a stamped document (read as written), else its clock.t0_us, through which
// the one rule (windowTimeUs, swing_analysis.h) takes an absolute value to window time and
// leaves a relative one alone. Every per-block typed reader takes that origin.
qint64 analysisReadOriginUs(const QJsonObject &root);

// The document's `analysis` block read back TYPED (TimeBase::Window), through the per-block
// readers and analysisReadOriginUs — an old absolute-time document and its rebased twin read to
// the same values. Reading never rewrites the document. Not read back (writer-only blocks):
// assessment, timings, bindings, clubDtl, club3d, skeleton3d, addressMarks.
analysis::SwingAnalysis analysisFromDocument(const QJsonObject &root);

// ── The one projection (refactor step 1.3b; audit H-01) ──────────────────────────────────────────
// The QML map a shot's analysis is shown from — ShotListModel's analysisDetail role, ShotReplay's
// analysisDetail, ShotProcessor's replayAnalysisDetail — built from the DISK FORM of the analysis
// block, so a live, a replayed and a reloaded shot read one map, key for key. Window time: `originUs`
// is analysisReadOriginUs(root), through which every instant goes by windowTimeUs (a stamped
// document is copied as written). Keys: tier, overall, series (= analysis.metrics), phases, the
// score's siblings (pattern, blended, resemblance, interval, perRegion, perPhase), segmentation,
// pose2d, club, ball, impact, bindings, timings, kinematicSequence, gatesVersion + gates,
// addressMarks, and `dtl` (dtl_overlay_payload.h) — each present when its block is.
QVariantMap analysisDetailFor(const QJsonObject &analysis, qint64 originUs);
// The same from a document root; empty when it has no `analysis` block.
QVariantMap analysisDetailForDocument(const QJsonObject &root);
// A live shot's map: its in-memory analysis composed into the disk form the document gets
// (`windowT0Us` = the capture window's start on the analysis' clock; `manifest` names the DTL stream
// and carries the integrity blocks the whole-shot gate records come from), then the one projection.
// Equal to the reloaded document's map, except that the "Save pose keypoints" choice drops the pose
// tracks from the file and not from the live map.
QVariantMap liveAnalysisDetail(const analysis::SwingAnalysis &a, qint64 windowT0Us, const QJsonObject &manifest);

// Patch a manifest that is about to be written back so its "imuIntegrity" block
// reflects THIS pass rather than whatever capture concluded.
//
// ⚠ nullptr REMOVES THE KEY, AND THAT IS THE WHOLE POINT. Before this existed the
// block was written once at capture and then rode through every re-analysis
// verbatim (the document write replaces only schema/review/analysis), so a verdict
// reached by a build that could not yet tell a host-fused lane from a wG3 became
// permanent — the ⚠ badge on eleven perfectly good 2026-08-18 wrist swings. A
// caller that could not establish the capture-time filter, or found no checkable
// lane, passes nullptr and the document then makes NO claim. "Not checkable" must
// never be persisted as "checked and passed".
void applyImuIntegrity(QJsonObject &manifest, const ImuRefusionVerdict *v);

// ── The swing.json "captureIntegrity" block ──────────────────────────────────
//
// Frame-timestamp holes in the camera lanes (capture_integrity_check.h): the
// 2026-08-18 s3 stall that lost ~80 frames after impact and left the user none
// the wiser — and, since issue #53, each lane's coverage of the capture window
// (missing at the start / end, frames before impact, per camera; additive keys,
// absent from older blocks). Same two producers, same lifecycle as imuIntegrity:
// nullptr or a verdict that checked no camera REMOVES the block, so a re-analysis
// can only ever replace or withdraw a claim, never inherit one.
QJsonObject captureIntegrityJson(const CaptureIntegrityVerdict &v);
void applyCaptureIntegrity(QJsonObject &manifest, const CaptureIntegrityVerdict *v);

// The per-shot DATA WARNING, read from both integrity blocks of a manifest. Empty
// when neither block warns. Otherwise { capture: bool, imu: bool, holes, framesLost,
// worstHoleMs, preImpact, postImpact, worstMaxDeg } plus, only when true, the short-clip
// facts { startsAfterImpact, lateStart + missingStartMs, earlyEnd + missingEndMs } (issue #53).
// A late start or early end alone carries those facts WITHOUT `capture`: a ⚠, not an exclusion
// — facts, not sentences, so the QML tooltip and the session ledger word it themselves.
// The one place the rule "which blocks constitute a warning" is spelled; readSwingJson
// and the live join both call it.
QVariantMap dataWarningDetailFrom(const QJsonObject &manifest);

// The swing's overall score out of a stored "analysis" block, in either shape it has
// been written: a bare int (pinpoint.analysis/2) or the ScoreBreakdown object whose
// "overall" it is (/3+, serializeScore). 0 when the block has no score.
// The one place that rule is spelled — every reader of a stored score calls it (the summary,
// the full reader, the replay and the data viewer's Properties row), so none can fall back
// to `score.toInt()` and read a /3 object as 0.
int overallScoreFromAnalysis(const QJsonObject &analysis);

// The down-the-line top-out-of-view fact (analysis.clubDtl.summary.topOutOfView)
// as the three dataWarningDetail keys — dtlTopOutOfView, dtlHandsFromTopPx,
// dtlClubPx (px, rounded) — spelled once so the on-disk read above and the live
// join (shot_processor.cpp, which has the track in memory, not the JSON) agree.
// Never sets `capture`/`imu`: it does not exclude the shot from the assessment.
void insertDtlTopOutOfView(QVariantMap &detail, double handsFromTopPx, double clubPx);

// The single, unified per-shot document. Raw capture manifest and derived analysis
// live in ONE swing.json — no separate analysis.json.
//
// ⛔ ITS WRITES ARE PRIVATE (refactor step 1.4). Every write of a swing file goes through
// RecordingRepository (src/DomainModel/recording_repository.h), implemented by
// SwingStoreRecordingRepository (swing_store_repository.h), the only friend that writes — the live
// join included, which writes through the repository's create() since step 1.5. What stays public
// here are reads.
//
// EVERY WRITE REFUSES A READ-ONLY DOCUMENT — one saved by a newer build (its schema, or its
// analysisContext block's, is newer than this build reads: readAnalysisContext, recording_header.h).
// It is never rewritten, so a newer build's fields are never lost to an older one.
class SwingDocWriter {
public:
    // The late-capture vocabulary lives with the repository; these names are kept for readers.
    using StreamOrigin   = pinpoint::StreamOrigin;
    using DeviceOnlyMeta = pinpoint::DeviceOnlyMeta;

    // ⚠ THE DOCUMENT WE JUST WROTE, kept so the next reader does not fetch and re-parse it
    // (2026-09-16). At the end of a shot the GUI thread serialised ~28 MB, wrote it to the library —
    // a network share in the studio — and then the replay immediately read the same file back and
    // parsed it again, a third full pass over the same pose track while the user waited.
    //
    // Exactly one entry, keyed by swing directory, holding the root writeDocument() already had in
    // hand. Every writer that rewrites a document invalidates it, so a cached copy can never be older
    // than the file, and TAKING IT CLEARS IT — one reader, once. Empty for a swing written by another
    // process or a previous run: callers fall back to reading the file, which is always correct and
    // only slower.
    static QJsonObject takeJustWritten(const QString &swingDir);

    // (The shot kind — captured or device-only — is read by readShotKind, recording_header.h: the
    // document's recorded `shotKind`, else the one content rule for a document older than it.)

    // Is this document read-only to this build — saved by a newer one (its schema, or its
    // analysisContext block's, is newer than this build reads)? Every write below asks first, and a
    // true answer fills *out (ReadOnly, with the reason) and writes nothing.
    static bool refusesWrite(const QJsonObject &root, WriteResult *out);

private:
    friend class SwingStoreRecordingRepository;   // THE door

    // Compose `rawManifest` (the exporter's returned pinpoint.swing tree) with the
    // analyzer's `analysis` (inline, additive "analysis" object) and write atomically.
    // Stamps the one schema constant. analysis==nullptr → raw only (no "analysis").
    // Refuses a read-only rawManifest (on a write-back it is the document on disk).
    //
    // `club` is the session's active club at the instant of the shot; it seeds the review
    // block, which editReview() later replaces if the user corrects it. Empty writes no
    // review block. WITHOUT IT a camera swing recorded its club nowhere a reader looked,
    // and every shot the user never rated read back as the club_vocabulary.h stub.
    //
    // `savePose` false (AppSettings savePoseKeypoints off) omits the pose tracks — analysis.pose2d
    // and poseDtl, about half of a document — and their version stamps, so re-analysis knows to
    // re-run pose rather than reuse a track that is not there. The replay then has no skeleton.
    //
    // THE WHOLE-SHOT GATE RECORDS (manifest_gates.h) are appended here, and only here: re-derived
    // from rawManifest's captureIntegrity / imuIntegrity blocks on every write.
    static WriteResult writeDocument(const QString &swingDir, const QJsonObject &rawManifest,
                                     const analysis::SwingAnalysis *analysis,
                                     const QString &club, bool savePose);

    // Write-through of the golfer's review edits into an existing document: reads the doc, MERGES
    // the edit into the additive "review" block (ReviewEdit, recording_repository.h), atomic
    // rewrite. A shot whose document was never written (e.g. export failed) fails here harmlessly
    // (NoDocument).
    // ⚠ MERGES rather than replaces: the block carries the rating, note and club (the swing-edit
    // popover) and the launch monitor board's annotations (excluded, clubOverride), and a star
    // rating must not erase a shot the golfer left out. The annotations are written ONLY when set —
    // false and "" REMOVE the key — so a document nobody annotated stays exactly as it was, and
    // absence keeps meaning "never annotated" (PersistedShot::excluded); an annotation-only edit is
    // a no-op (Unchanged) when nothing would change, and never touches the club, so a document with
    // no declared club does not acquire the stub. A rating / note / club edit always rewrites.
    static WriteResult editReview(const QString &swingDir, const ReviewEdit &edit);
    // The shot-level golfer input on the review (shotIntent, labelCorrections), read from the document's
    // root alone. A document without them reads as none.
    static ShotReview readReview(const QString &swingDir);

    // The archive's stub (FILE-SHOT-10) written over the document, as given — refused when the document
    // is read-only (a newer build's; the stub carries its schema and analysisContext) or absent. Drops
    // the just-written copy.
    static WriteResult writeArchiveStub(const QString &swingDir, const QJsonObject &stub);

    // Fold a launch monitor reading into an existing document.
    //
    // LATE BY DESIGN. The monitor writes its file after the shot, and swing.json is
    // written only when the analyzer and exporter have both finished (12-37 s), so a
    // reading normally lands during analysis and sometimes during the first replay.
    // Rather than hold the document open or re-run analysis, this is the same
    // read-modify-atomic-rewrite editReview() performs, and it writes two things:
    //
    //   · a top-level "launchMonitor" block — every value as the device reported it,
    //     including the columns that map to no metric. Additive, so older readers
    //     ignore it (swing_json_schema.md).
    //   · one entry per reading in analysis.metrics[], keyed `lm.*`, each an EMPTY
    //     CURVE carrying a single phaseSample at Impact. That is the shape every
    //     point-in-time metric already uses, and it is what lets the phase grid,
    //     the measures, the corridors, the charts and the data viewer pick these up
    //     with no new machinery anywhere.
    //
    // The metric entries are only written when the document already has an "analysis"
    // block; the raw block is written regardless, so a shot whose analysis failed
    // still keeps what the device said.
    //
    // Idempotent: re-applying replaces any existing "launchMonitor" block and any
    // existing `lm.` metric entries rather than appending duplicates. Refreshes the
    // summary for the same reason editReview does — this rewrite moves the document's
    // size and mtime, which invalidates the sidecar guards.
    //
    // `captureClub`, when not empty, REPLACES capture.club in the same write: the club the
    // swing was recorded under, with its geometry (lengthMm, bandCentersMm, …, name). Used only
    // when the launch monitor names the club and the user asked PinPoint to follow it
    // (launchmonitor/premierFollowClub; IM-4). Replaced only when its name differs from the recorded
    // one, and never over a hand-set review.club: one naming neither the recorded club nor the
    // device's (the live save seeds review.club with the session's club, so a seeded one moves
    // with capture.club). The same write replaces the analysisContext block's captured club
    // (withFollowedClub, recording_header.h; R-REC-2(a), DEC-82) — what re-analysis reads — so the
    // input stamp marks the analysis stale. *clubReplaced says whether it was, so the caller can re-analyse.
    static WriteResult updateLaunchMonitor(const QString &swingDir,
                                           const lm::LaunchMonitorReading &reading,
                                           const QJsonObject &captureClub,
                                           bool *clubReplaced);

    // Rebuilds the document's `lm.` rows and the rows WE derive from them (lmDerivedKeys:
    // compoundMiss, lowPointFromAttack) from the raw `launchMonitor` block already in the file,
    // through the same emitter as updateLaunchMonitor(). THE RAW BLOCK IS NOT REWRITTEN: it is
    // what the device said, and nothing here has anything new to say about it.
    //
    // WHY: a derivation added or changed after a swing was paired reaches a swing with video when
    // it is re-analysed (writeDocument derives again from the raw block), but a launch-monitor-
    // only swing (writeDeviceOnlySwing) has nothing to re-analyse, so it kept the rows of the day it
    // was written. The 4 Aug 2026 session's 25 shots had no compoundMiss, so pull-hook and
    // push-slice could not be judged on any of them. Re-analysis of such a swing calls this.
    //
    // Written when the rows change; Unchanged when they do not (Written on a dry run = WOULD be).
    // A document with no raw block, or no analysis block, is left alone and is not an error.
    static WriteResult refreshLaunchMonitorRows(const QString &swingDir, bool dryRun);

    // THE STEP-1 BACKFILL (D4, DEC-34; RecordingRepository::backfillAnalysisContext): writes the
    // `analysisContext` block into a document that has none — its legacy context with the athlete
    // record's height and club set on the swing's date (backfilledAnalysisContext, recording_header.h).
    // A document that already has a block is never rewritten (Unchanged), nor one whose facts add
    // nothing to the legacy context it reads today (Unchanged: a block that only froze "unknown"
    // would stop a later backfill from filling it). Written on a dry run = WOULD be; nothing written.
    static WriteResult recordBackfilledContext(const QString &swingDir, const AthleteRecordFacts &facts,
                                               bool dryRun);

    // ── CORE §8.5 — where a stream's bytes came from, when they came off a wire ──
    //
    // ⭐ PROVENANCE, AND ONLY PROVENANCE.  This is the swing.json half of the link
    // the PPCP ledger holds: the ledger keys on the opaque identity and remembers
    // which swing it landed in, and this remembers the opaque identity beside the
    // stream it became.  Either can be rebuilt from the other, which is the point
    // of linking the two identity schemes rather than merging them (CORE 8.5a/8.5b,
    // "no entity is rewritten or merged").
    //
    // ⛔ NOTHING IN src/Analysis MAY KEY ON THIS.  It is recorded, never
    // interpreted — the same rule, and for the same reason, as the existing
    // `capture.host` block: where a frame arrived from is not a property of the
    // swing, and a measure that varied by transport would be measuring the network.
    // Asserted by CT-I37's sibling in swing_doc_test.
    //
    // Additive on a `streams[]` element, so every existing reader ignores it —
    // they select by `kind` and read named keys (swing_export_developer_guide §6).
    //
    // Attach or refresh the `origin` of ONE `streams[]` element, by alias.
    //
    // LATE BY DESIGN, exactly as updateLaunchMonitor() is: a clip is asked for at
    // the shot and arrives seconds later, long after swing.json was written, and
    // the answer may be "I no longer have it".  So this is the same
    // read-modify-atomic-rewrite, and it is idempotent — re-applying replaces the
    // block rather than appending beside it.
    //
    // Creates the `streams[]` element when no element carries `alias`, so the
    // pending state of a clip that has not landed can be recorded before there is
    // any file to point at.  ⚠ Such an element deliberately has NO `file` key
    // until the bytes are on disk: the replay source and the disk loader both skip
    // an element with no readable file, which is the behaviour we want while a
    // transfer is in flight.
    //
    // `fileName` names the file the bytes became, relative to the swing folder.
    // Written only when non-empty, because the two facts arrive together and for
    // the same reason: an element that says `complete` while naming no file is a
    // stream no reader can open, which is worse than one that says it is still
    // arriving.  Left alone on a pending or absent update, so a later state
    // change never erases a file that has already landed.
    // `frameTUs` are the per-frame timestamps in microseconds, in the same
    // window-relative convention every other video stream uses unless `framesClock`
    // (below) names another clock.  Written only
    // when non-empty, and REQUIRED for the stream to be usable: the Recording loader
    // and DiskReplaySource both skip a video element whose `frames.t_us` is
    // empty, so a clip that lands without them is a file on disk that nothing
    // will ever open.
    // `playbackFps` is the FILE's frame rate -- what the container plays at,
    // which for a phone clip is its capture rate.  ⛔ Without it the replay
    // defaults to 30, decides 719 frames are 24 s of video to fit into a 3 s
    // window, runs the player at 8x and the clip is over in half a second
    // (2 Sept 2026).  Measured from the frame times, never assumed.
    // `framesClock` names the clock `frameTUs` run on when it is NOT the window's: the phone path
    // passes SwingStore::kFramesClockClip ("clip"), because ppcp_clip_filer.cpp times a clip from its
    // own first frame. Written as `frames.clock` beside `frames.t_us`; empty writes nothing (the
    // window's clock). SwingStore::streamOnWindowClock is its reader (issue #53).
    static WriteResult updateStreamOrigin(const QString &swingDir, const StreamArrival &arrival);

    // Write a complete document for a shot that ONLY a launch monitor saw.
    //
    // The capture pipeline produced nothing: no camera, no IMU, no buffer window, so no
    // analysis and no export. This is not a degraded version of writeDocument — it is a
    // different document, and it says so rather than leaving blanks that read as failure:
    // `streams` is empty, there is no thumbnail, and `analysis` carries metrics and one
    // phase and nothing else.
    //
    // THE IMPACT EVENT IS MANUFACTURED, AND IT HAS TO BE. Every lm.* measure reduces
    // "at p7", and buildPhaseGrid returns an EMPTY grid when `analysis.phases[]` is empty
    // — "unsegmented: no phase to read anything at, so nothing is producible" — so
    // without a phase entry the readings would persist and then resolve to nothing at
    // all. A ball WAS struck, which is why `conf` is 1.0: what is unknown is the
    // instant, not the event. That unknown is carried honestly by `capture.impactUs`,
    // which stays at its -1 "unknown" sentinel while the phase sits at t_us 0.
    //
    // Failed if the reading has no values or the directory cannot be written.
    static WriteResult writeDeviceOnlySwing(const QString &swingDir,
                                            const lm::LaunchMonitorReading &reading,
                                            const DeviceOnlyMeta &meta);

    // Convert a JSON-era swing to swing.ppsw, with this writer's `summary` block, and prove it
    // before deleting anything (SwingStore::convertDir). The library conversion — in-app and
    // pps_convert_library — both come through here, so a converted swing is indistinguishable
    // from one this writer wrote. A read-only (newer) JSON document is refused (Failed).
    static SwingStore::ConvertResult convertToPpsw(const QString &swingDir, bool deleteJson);

};

// A reloaded shot — a swing.json read back whole. The flat metrics + analysisDetail are
// reconstructed into the same shapes ShotProcessor produces live.
struct PersistedShot {
    bool        ok = false;
    QString     swingDir;
    int         ordinal = 0;
    QString     timestampLabel;     // hh:mm:ss from clock.wallclock
    qint64      wallclockMs = 0;    // absolute instant from clock.wallclock (epoch ms; 0 = unknown)
    // WHO swung it. swing.json has carried these since the exporter's first version and the reader
    // dropped them, so a reloaded shot knew when it happened and not who it belonged to.
    //
    // The uuid is what a norm COHORT is resolved through: the athlete record holds the date of
    // birth, `wallclockMs` holds the day, and the band is derived from the two at read time — never
    // stored, because an athlete ages across their own history. Without the uuid here, an offline
    // re-analysis and the live path would resolve different cohorts for the same swing and grade it
    // two ways, with nothing anywhere reporting a disagreement.
    QString     athleteUuid;
    QString     athleteName;        // display only; the uuid is the key
    QString     handedness;         // "Left", "Right", or "" on a document written before it was recorded
    QString     club;
    bool        hasVideo = false;
    QString     thumbnailPath;      // absolute, empty if none
    int         score = 0;
    int         rating = 0;         // 0–5 user stars (from the "review" block)
    QString     note;               // free-text user note (from the "review" block)
    QVariantMap metrics;            // key -> { label, value } at Impact
    QVariantMap analysisDetail;     // the one projection (analysisDetailFor): window time, the live map's keys
    bool        dataWarning = false;// an integrity block warns (dataWarningDetailFrom): IMU re-fusion
                                    // parity failed, or frames were lost during capture
    QVariantMap dataWarningDetail;  // the facts behind it (empty when !dataWarning)
    // HOW IT ARRIVED AND WHAT MEASURED IT, two facts (lm_device.h). lmDeviceKind is the
    // connector — launchMonitor.kind ("premier"); lmDevice is the device — deviceKeyOfBlock()
    // ("gcquad"), which is what every caption names. Both empty when the shot has no device block.
    // The readings themselves come back through analysisDetail's `lm.` series; this is the only
    // place their provenance survives a reload, and without it a reviewed session could only be
    // captioned with whatever is plugged in today.
    QString     lmDeviceKind;
    QString     lmDevice;
    // WHAT RECORDED IT — the swingDeviceTokens() of the document (cameras by perspective,
    // HackMotion, other IMUs, the launch monitor). The session picker shows and filters on these.
    QStringList devices;
    // THE LAUNCH MONITOR BOARD'S TWO USER ANNOTATIONS, from the "review" block (review.excluded,
    // review.clubOverride). Both are the golfer's own decisions made on the panel's pinned card —
    // "Leave out" and "Club" — and both are OPTIONAL keys: absent means not left out and no
    // override, which is also exactly what every document written before they existed means, so
    // absence is unambiguous (swing_json_schema rule: decide what absence means). User
    // annotations, not analysis output — no stage version moves with them.
    //   excluded      the shot is left out of the board's means, spreads, grades and ellipses
    //   clubOverride  the club the board treats the shot as, "" for the default (the club
    //                 evidence's suggestion, else the recorded club); the recorded club itself,
    //                 chosen explicitly, declines the evidence
    bool        excluded = false;
    QString     clubOverride;
};

// The handful of fields a session-list row needs — everything pinpoint::ShotSummaryInput
// (session_summary.h) consumes, plus ordering/display scalars. Deliberately NOT a subset
// view of PersistedShot: that struct carries analysisDetail (incl. the multi-MB pose2d
// keypoint track needed for replay overlays), and the whole point here is to summarise a
// session WITHOUT touching it.
// The devices that recorded a swing, as stable tokens in a fixed order, from the document's
// `streams` and `launchMonitor` blocks (only kind/alias/file/setup/instrument of a stream are read,
// so a root with its per-frame columns trimmed answers the same):
//   "faceOn" / "dtl" / "impact" / "camera"  a video stream WITH a file, by setup.perspectiveName
//                                           ("camera" = any other or no perspective)
//   "hackmotion"                            an imu stream with instrument "hackmotion"
//   "imu"                                   any other imu stream (Witmotion)
//   "lm:<device>"                           the launch monitor's DEVICE, lm_device.h deviceKeyOfBlock()
//                                           ("lm:gcquad" whether FSX2020 or Premier read it; "lm:gspro"
//                                           for a device GSPro relayed without naming one we know)
// A video stream without a file is a clip still owed (see hasVideo) — not recorded, so not listed.
QStringList swingDeviceTokens(const QJsonObject &root);

struct SwingSummary {
    bool    ok = false;
    QString swingDir;
    int     ordinal = 0;
    QString timestampLabel;         // hh:mm:ss from clock.wallclock
    qint64  wallclockMs = 0;        // absolute instant (epoch ms); 0 = unknown
    QString club;
    bool    hasVideo = false;
    QString thumbnailPath;          // absolute, empty if none
    int     score = 0;
    // The shot carries a data warning (PersistedShot::dataWarning). Here because the
    // session ledger judges shots from this cheap read and must never count one whose
    // recording is known to be broken.
    bool    dataWarning = false;
    // ⚠ WHAT A CAROUSEL ROW NEEDS, added 2026-09-16 so ending a session stops fat-parsing every
    // swing (~28 MB each, seconds apiece in a debug build over a share). The card's required
    // properties are filled from model roles, so a row rebuilt without these loses its metric
    // chips, its stars and its warning tooltip — which is why they live here rather than the
    // session load simply dropping them. All small: metrics is a few dozen key→{label,value}.
    //
    // analysisDetail is deliberately NOT here — the multi-MB pose track is the thing this read
    // exists to avoid. A reloaded row carries none; its one reader, the launch monitor board, reads
    // the row's Shot's measurements (Shot::measurements; refactor 6.1a).
    QVariantMap metrics;
    int         rating = 0;         // 0–5 user stars, from the "review" block
    QString     note;               // free-text user note, same block
    QString     lmDeviceKind;       // launchMonitor.kind token (the connector), empty when no device block
    QString     lmDevice;           // the device (lm_device.h deviceKeyOfBlock), empty when no device block
    // What recorded the swing (swingDeviceTokens), for the session picker's device tags and filter.
    // Additive to /4 like `excluded` below — but an older block lacking it is NOT right as it stands,
    // so the swing.ppsw read derives it from the root chunk's streams it has already decoded, and a
    // JSON-era sidecar without it is treated as stale.
    QStringList devices;
    QVariantMap dataWarningDetail;  // the facts behind dataWarning (empty when there is none)
    // The launch monitor board's annotations (PersistedShot::excluded / clubOverride), so a
    // session loaded through the cheap path keeps them. ADDITIVE in the /4 summary rather than a
    // schema bump: a bump would send every existing swing.ppsw back to the full parse on the next
    // session load (the stall /4 exists to prevent), and an older summary lacking the keys is
    // CORRECT as it stands — it was written before any annotation could exist, and any write
    // that adds one rebuilds the summary from the document.
    bool        excluded = false;
    QString     clubOverride;
    // The golfer's handedness (document `athlete.handedness`, "Left"/"Right"/""), so a shot's words can
    // be mirrored without a parse. Additive to /4 like `devices`, and for the same reason NOT right when
    // absent: a swing.ppsw block without it derives it from the root chunk it already decoded, and a
    // JSON-era sidecar without it is stale (readSwingSummary).
    QString     handedness;
    // Provenance, never persisted: true when this came from the sidecar, false when the
    // full swing.json had to be parsed. Lets the parity test prove it actually exercised
    // the cheap path — a bug that always fell back would otherwise pass silently while
    // the stall crept back.
    bool    fromSidecar = false;
};

// Reads the single unified swing.json back into reloadable shots.
class SwingDocReader {
public:
    static PersistedShot readSwingJson(const QString &swingDir);

    // (The launch monitor board's own slice read, readLaunchMonitorDetail, went in refactor 6.1a: the board reads its
    // rows' Shots' measurements, RecordingRepository::readMeasurements.)

    // The `origin` of one `streams[]` element, by alias. Empty when the swing has
    // no such stream, or when that stream carries no origin — which is every
    // stream from a directly attached camera, and correctly so: a USB camera's
    // frames did not come off a wire and have no opaque identity to record.
    static SwingDocWriter::StreamOrigin streamOrigin(const QString &swingDir,
                                                     const QString &alias);

    // Cheap per-swing summary for the session picker. Prefers <swingDir>/swing_summary.json
    // (a few hundred bytes), validating its source{size,mtime_ms} against the real
    // swing.json; on a miss or a stale guard it falls back to the full readSwingJson()
    // parse and — when writeSidecar is true — writes the sidecar so the next read is cheap.
    //
    // Pass writeSidecar=false on any GUI-thread path that must never fat-parse: the caller
    // then gets ok=false for an un-indexed swing and renders it without detail, rather than
    // stalling. The sidecar is pure cache — always safe to delete, always regenerable.
    //
    // The SidecarPolicy form (src/Core/sidecar_policy.h; refactor 3.7-0 split the one bool): the bool is
    // CachedOnly (false) or ParseAndWrite (true), as above; ParseNoWrite derives the summary from the
    // document on a miss or a stale guard exactly as ParseAndWrite does, but writes no sidecar — the
    // parallel run's firing reader, which must leave every file of the session to its one writer.
    // (A .ppsw's own block is never written back by any policy: see the reader.)
    //
    // `parsed`, when given, is where a fallback parse comes from (SwingStore::LazyDocument, #142): a
    // caller that also reads the swing's phase grid shares one, so a fresh swing is parsed once.
    static SwingSummary readSwingSummary(const QString &swingDir, bool writeSidecar = true,
                                         SwingStore::LazyDocument *parsed = nullptr);
    static SwingSummary readSwingSummary(const QString &swingDir, SidecarPolicy policy,
                                         SwingStore::LazyDocument *parsed = nullptr);

    // Derive a summary from an already-parsed shot and write its sidecar. Called where a
    // fat parse has happened anyway (session load), so indexing costs nothing extra.
    static bool writeSwingSummary(const PersistedShot &shot, QString *error = nullptr);
    // A summary a reader parsed, indexed as the JSON-era sidecar (swing_summary.json; a .ppsw carries its summary inside,
    // so nothing is written for one). For a reader that read with ParseNoWrite and writes the index itself, under the
    // session's lock (SwingStoreFiringReader; refactor 3.8-ii, R1-m6).
    static bool writeSummarySidecar(const SwingSummary &summary, QString *error = nullptr);
    // swing_*/ directories under sessionDir, ascending (swing_0001 .. swing_NNNN).
    static QStringList findSwingDirs(const QString &sessionDir);
    // Most recent session dir in an athlete's folder (Athlete::folder(); never derived from a
    // name: step 2.7a), by directory modification time (folder names embed the naming pattern, so
    // a name sort isn't reliable); empty if the athlete has none.
    static QString latestSessionDir(const QString &athleteDir);
    // All session dirs in an athlete's folder, most-recently-modified first (same recency basis
    // as latestSessionDir). Empty list if the athlete has none.
    static QStringList sessionDirs(const QString &athleteDir);
};

} // namespace pinpoint
