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

// RecordingRepository — THE ONE DOOR TO A SWING FILE (refactor step 1.4; docs/developer/
// information_model.md §2 "The Recording contract" and §8 "Persistence"; step 1 design §4.8).
//
// Every write of a shot's `swing.ppsw`, its archive stub and its markup truth goes through this
// interface, KEYED BY THE SHOT'S ShotRef (step 2.4; IM-S2-2: DEC-160). The folder is resolved through
// SessionRepository (the session's identity map) and is a property and provenance only, never a key.
// SwingStoreRecordingRepository (src/Export/swing_store_repository.h) implements it; the composition
// root (main.cpp, each tool's main) holds the one instance and hands it to the callers, so a caller
// names this interface and never the store.
//
//   shotOfFolder      THE EDGE TRANSLATION (DEC-90): a swing folder named by a path — a tool's
//                     argument, a picker, a row that still carries one until step 4 — to its ShotRef,
//                     once, where the path comes in. A session without a session file (every session
//                     before M2) resolves to D3's derived id (DEC-150)
//   swingFolder       the shot's folder, as a property (its media, its log lines)
//   create            the FIRST write: header + streams (+ analysis) and the seeded review club;
//                     or a shot the launch monitor alone recorded
//   completeCapture   a LATE part of the Recording (IM-4: capture writes in stages): a launch
//                     monitor reading (which may carry the club the device named: follow-club), or
//                     a phone clip's stream origin
//   writeAnalysis     replaces the analysis and both integrity verdicts (the re-analysis
//                     write-back). Keeps the empty-document guard; honours PersistOptions. With no
//                     analysis, the launch monitor rows are derived again from the recorded reading
//                     instead (a launch-monitor-only shot's re-analysis; lm_repair)
//   editReview        merges the golfer's edits into the `review` block (rating, note, club,
//                     exclusion, club override, and — IM-S2-3 — the shot intent and the label
//                     corrections)
//   readReview        the golfer's shot-level input read back (the shot intent, the label
//                     corrections); a document older than the keys reads as having none
//   readSummary       the shot's SUMMARY as recorded (ShotSummary: what a carousel row shows of it), by the
//                     cheap read (the document's summary block; never the multi-MB analysis): what the Shot
//                     answers its summary() from (refactor step 3.4b-ii: the carousel's rows over the Shots)
//   readMeasurements  the shot's MEASUREMENTS (OBJ-MEASUREMENT; refactor 6.1a): the one source-tagged set, parsed from
//                     its analysis as stored (the root chunk and the metrics, never a per-frame track): what the Shot
//                     answers its measurements() from. An empty set when there is no document
//   convert           a JSON-era swing → swing.ppsw, proven before the JSON is deleted
//   writeSummarySidecar  the JSON-era swing_summary.json cache
//   writeArchiveStub  the archive's stub (FILE-SHOT-10): the document thinned to what stays in the
//                     library, written over the shot's document
//   writeTruth, truth the video markup's ground truth (FILE-SHOT-12; DEC-83): truth.json (face-on),
//                     truth_dtl.json (down the line)
//   backfillAnalysisContext  THE STEP-1 BACKFILL (D4; DEC-34): a one-off migration Mark approves
//                     (R-REC-1, R-PERS-7) that records the analysis context of a document older than
//                     the block, with the height and club set its capture never wrote
//   isDeviceOnly      a read: did the launch monitor alone record this shot? The shot kind
//                     (R-REC-8; DEC-44, DEC-87): the one header writer records it at creation, and
//                     a document older than the field is classified here, once, by today's rule
//
// THE WRITER CONTRACT (R-REC-1; DEC-86): capture CREATES the shot (create, then its late parts through
// completeCapture); after that only the Shot writes it — until step 3, today's callers of R-REC-3 on its
// behalf (below); everything else READS. The only other writes are one-off migrations Mark approves
// (backfillAnalysisContext).
//
// A SHOT THAT CANNOT BE RESOLVED — a null ShotRef, or one whose session is not open in the session
// repository — is never guessed at: every write answers Failed ("not found"), every read answers empty.
//
// ⛔ EVERY WRITE REFUSES A READ-ONLY DOCUMENT: one saved by a newer PinPoint Studio (its schema, or
// its analysisContext block's, is newer than this build reads). Such a file is never rewritten
// (WriteResult::Status::ReadOnly), so a newer build's fields are never lost to an older one.
//
// INTERIM (IM-5; Mark, 14 Nov 2026): until step 3 the callers are today's — ShotListModel,
// ReanalysisController, the launch monitor controller, the phone-clip filer, the archiver, the
// library converter, the markup tool and the tools — all through this interface; the `Shot` takes
// them over in step 3. The live join (ShotProcessor) writes through create() since step 1.5, with the
// ShotRef its allocation issued. Reading a Recording back is NOT here: it is the separate, small
// RecordingReader (recording_reader.h, step 1.5), so this write door does not pull frame decoding into
// every writer of a review.
//
// THREADS. Every method but shotOfFolder is safe from worker threads (the folder is resolved from the
// session repository's published folders, swingFolder). shotOfFolder reads the session into the
// identity map and so runs on its owner thread (the GUI thread in the app; a tool's main thread).
//
// Domain model ring (src/DomainModel): names no store type. The analysis and the launch monitor
// reading are passed by reference to their own (inner-ring) types, declared forward here.

#include "../Identity/identity.h"
#include "../Metrics/measurement.h"   // MeasurementSet (OBJ-MEASUREMENT; refactor 6.1a)

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pinpoint::analysis { struct SwingAnalysis; }
namespace pinpoint::lm { struct LaunchMonitorReading; }

namespace pinpoint {

struct ImuRefusionVerdict;        // IMU/imu_refusion_check.h
struct CaptureIntegrityVerdict;   // Analysis/capture_integrity_check.h
struct PersistedShot;             // the reloaded shot the JSON-era sidecar is derived from

// What a write did. ok() = the document is as asked (written, or already so).
struct WriteResult {
    enum class Status {
        Written,      // the document was rewritten (on a dry run: WOULD be)
        Unchanged,    // nothing to change; nothing written
        NoDocument,   // no document, or it could not be read: nothing written (the empty-document guard)
        ReadOnly,     // saved by a newer build: never rewritten by this one
        Failed,       // the arguments were refused, the shot could not be found, or the write failed
        Locked,       // another writer holds the folder's advisory lock (FolderLock, R-PERS-6) past the
                      // wait; nothing written. `error` names the holder (step 2.4: J-22)
    };
    Status  status = Status::Failed;
    QString error;   // why, for every status but Written / Unchanged

    bool ok() const      { return status == Status::Written || status == Status::Unchanged; }
    bool changed() const { return status == Status::Written; }
};

// How an analysis is persisted (design §4.7).
struct PersistOptions {
    // Storage → Save pose keypoints. false omits the pose tracks (analysis.pose2d / poseDtl, about
    // half a document) and their version stamps, so a later re-analysis runs pose again rather than
    // looking for a track that is not there. The app fills it from the setting; SwingLab keeps the
    // app's default (true).
    bool savePose = true;
    // writeAnalysis only: say whether the document WOULD be rewritten (Written) or not (Unchanged),
    // and write nothing (lm_repair's dry run). Every other write ignores it.
    bool dryRun = false;
};

// ── create ───────────────────────────────────────────────────────────────────────────────────────
// The first write of a camera / IMU / phone shot.
struct NewRecording {
    // The document's header and streams, from the one header writer (Export/recording_header.h),
    // with any integrity verdict the capture reached already in it.
    QJsonObject header;
    // The analysis, when there is one (none: the header alone, e.g. the phone-clip placeholder).
    const analysis::SwingAnalysis *analysis = nullptr;
    // The club the session was on, seeded into review.club when the document has none there yet.
    // Empty writes no review block.
    QString reviewClub;
};

// Who a launch-monitor-only shot belongs to and where it sits: everything its document needs that
// the reading cannot supply.
struct DeviceOnlyMeta {
    QString swingId;              // "swing_0007"
    int     swingIndex = 0;
    QString sessionId;            // the session FOLDER name, for the draw-from filter
    QString athleteName;
    QString athleteUuid;          // what a norm COHORT is resolved through
    QString club;                 // the app's selected club, not the device's code
    int     sessionType = -1;
    qint64  wallclockMs = 0;      // when the reading was taken
    // A STANDALONE shot: the launch monitor's own, in a folder it allocated, which no capture or
    // phone clip files into — so its kind is device-only for its whole life and the document records
    // it (`shotKind`, R-REC-8). False for the rescue path (a failed camera shot's folder, which a phone
    // clip may still join): no kind is recorded there, and the content rule reads it.
    bool    standalone = false;
};

// ── completeCapture ──────────────────────────────────────────────────────────────────────────────
// Where a stream's bytes came from, when they came off a wire (CORE §8.5). PROVENANCE ONLY: nothing
// in src/Analysis may key on it.
struct StreamOrigin {
    // ⚠ THE TWO WORDS THAT ARE NOT THE SAME WORD.
    //   `transfer`     — THIS HOST's view of the exchange. Ours to assert.
    //   `completeness` — the OWNER's assertion about the Capture (CORE 5.14, I10), carried verbatim
    //                    and NEVER inferred from how many bytes turned up.
    QString transport;      // "ppcp"
    QString peerId;         // the MINTING peer — I34's first part
    QString sessionId;
    QString captureId;
    QString streamId;
    QString transfer;       // requested|arriving|complete|absent|timeout|failed
    QString completeness;   // complete|partial|absent — the owner's word
    QString absentReason;   // 7.3b, e.g. "outside_buffer"; only when absent
    QString committedAt;    // ISO-8601 UTC; empty until capture_committed went

    bool isEmpty() const { return transport.isEmpty() && captureId.isEmpty(); }
};

// A phone clip's late part: its origin on the `streams[]` element `alias` (created when no element
// carries it), and, once the bytes landed, the file, its frame times and its frame rate.
struct StreamArrival {
    QString         alias;
    StreamOrigin    origin;
    QString         fileName;       // relative to the swing folder; empty = not landed (not written)
    QVector<qint64> frameTUs;       // per-frame µs; empty = not written
    double          playbackFps = 0.0;   // the FILE's rate; 0 = not written
    QString         framesClock;    // the clock frameTUs run on when not the window's ("clip")
};

// ── writeAnalysis ────────────────────────────────────────────────────────────────────────────────
// A re-analysis to persist. The verdicts REPLACE the document's blocks: nullptr removes one, so a
// re-analysis can only replace or withdraw a capture-time claim, never inherit it.
struct AnalysisOutcome {
    // None: the analysis is kept and the launch monitor rows are derived again from the recorded
    // reading (the verdicts are then not touched) — what re-analysing a launch-monitor-only shot is.
    const analysis::SwingAnalysis *analysis = nullptr;
    const ImuRefusionVerdict      *imuIntegrity = nullptr;
    const CaptureIntegrityVerdict *captureIntegrity = nullptr;
};

// ── editReview, readReview ───────────────────────────────────────────────────────────────────────
// A golfer's correction to one of the shot's labels (FILE-SHOT-02 `review.labelCorrections`; DEC-13):
// `family` the label family, `from` what was detected, `to` what it should have been ("" = none in that
// family). The same three fields as diagnostics.json's `corrections` entries today (Coaching's
// ShotCorrection), so moving them (step 2.5, migration M3) is a copy.
struct LabelCorrection {
    QString family, from, to;
    friend bool operator==(const LabelCorrection &a, const LabelCorrection &b)
    {
        return a.family == b.family && a.from == b.from && a.to == b.to;
    }
};

// The golfer's edits, MERGED into the review block: an unset field is left as it is, so a star
// rating never un-excludes a shot. A rating, note or club edit always rewrites (and so re-indexes)
// the document; an exclusion, override, shot-intent or label-correction edit writes only when it
// changes something, and removes its key when set back to the default (false / "" / no corrections),
// so absence keeps meaning "never annotated".
struct ReviewEdit {
    std::optional<int>     rating;        // 0–5, clamped
    std::optional<QString> note;
    std::optional<QString> club;          // the popover's (declared) club
    std::optional<bool>    excluded;      // left out of the launch monitor board's statistics
    std::optional<QString> clubOverride;  // the board's club for the shot ("" = the default)
    // IM-S2-3 (DEC-160): the shot's own golfer input, on its review. Written by the diagnostics model's
    // commands since step 2.5; the older copy in diagnostics.json is moved by migration M3 (2.8).
    std::optional<QString>                  shotIntent;        // "fade", "draw", … ("" = none)
    std::optional<QVector<LabelCorrection>> labelCorrections;  // the whole list (empty = none)
    // An empty shot intent or list is written as the key with an empty value ("" / []) — the golfer's
    // explicit "none" — instead of removing the key. Only while diagnostics.json's older copy still holds
    // an answer for the shot (step 2.5, the lead's call J-33): a removed key would let that older answer be
    // read again as the fallback. Never set otherwise, so absence keeps meaning "never annotated".
    bool keepEmptyGolferInput = false;
};

// The shot-level golfer input, as the review block holds it. A document older than the keys, or one
// whose review holds none, reads as none (empty), which is what it meant before them.
struct ShotReview {
    bool                     ok = false;   // false: no document, or the shot could not be found
    QString                  error;
    QString                  shotIntent;
    QVector<LabelCorrection> labelCorrections;
    // Whether the review holds each key at all (step 2.5): a held key — empty included — is the shot's
    // answer; an absent one sends the reader to diagnostics.json's older copy until migration M3 (J-33).
    bool                     hasShotIntent = false;
    bool                     hasLabelCorrections = false;
    // The launch monitor board's annotations on the same review block (review.excluded, review.clubOverride), read
    // with the rest for the reductions that honour them (refactor step 3.6; Stage 0.5's follow-up;
    // Coaching/board_annotations.h). Written by the Shot's commands (excludeReading, correctClub), never here.
    bool                     lmExcluded = false;
    QString                  lmClubOverride;
};

// ── readSummary (refactor step 3.4b-ii: the carousel's rows over the Shots) ─────────────────────────────────
// THE SHOT'S SUMMARY, as its document records it: everything a carousel row shows of the shot, read by the cheap
// path (the document's summary block — the JSON-era sidecar for a document older than swing.ppsw — never the
// multi-MB analysis: the read Export's SwingDocReader::readSwingSummary has made for the carousel since
// 2026-09-16). The same fields with the same values that read gives, so a row built from it is today's row.
// The Shot keeps the one it read (Shot::summary) and updates it with its own commands' saves.
struct ShotSummary {
    bool        ok = false;          // false: no document yet, or it could not be read
    int         ordinal = 0;         // the swing number the document records
    QString     timestampLabel;      // hh:mm:ss of the capture
    QString     club;                // the club the row names: the review's declared club, else the captured
                                     // club, else the stub (club_vocabulary.h)
    bool        hasVideo = false;
    QString     thumbnailPath;       // absolute; "" when none
    int         score = 0;
    QVariantMap metrics;             // key → { label, value }
    int         rating = 0;          // the golfer's stars, 0–5 (review.rating)
    QString     note;                // review.note
    QString     lmDevice;            // the device that measured it (lm_device.h key); "" for none
    bool        dataWarning = false;    // an integrity block warns
    QVariantMap dataWarningDetail;      // the facts behind it
    bool        lmExcluded = false;     // the launch monitor board's exclusion (review.excluded)
    QString     lmClubOverride;         // the board's club correction (review.clubOverride; "" = none)
};

// ── writeTruth, truth (FILE-SHOT-12) ─────────────────────────────────────────────────────────────
// Which camera's ground truth: the markup tool's two sidecars (face-on pixel space and down-the-line
// pixel space never cross; Gui/markup/markup_truth.h says why).
enum class TruthView { FaceOn, DownTheLine };

// ── convert ──────────────────────────────────────────────────────────────────────────────────────
struct ConversionResult {
    enum class Outcome { Converted, AlreadyConverted, NoDocument, Failed };
    Outcome outcome   = Outcome::Failed;
    qint64  jsonBytes = 0;
    qint64  ppswBytes = 0;
    QString error;     // Failed: what failed (a newer document is refused here too)
};

// ── backfillAnalysisContext (the step-1 backfill; D4, DEC-34) ────────────────────────────────────
// The athlete facts the old capture never wrote, as the athlete's record holds them. The store
// turns them into the context the shot would have recorded (backfilledAnalysisContext,
// Export/recording_header.h): the legacy context, with the height and the club set ON THE SWING'S
// DATE. A height the document itself recorded at capture (athlete.heightM) wins over the record's.
struct AthleteRecordFacts {
    double  heightM = 0.0;   // standing height from the record; 0 = unknown (left unknown)
    QString sex;             // the record's spelling ("male" / "female" / ""); "" = unknown
    QString dobIso;          // yyyy-MM-dd; "" = unknown (no junior set can be decided)
};

class RecordingRepository {
public:
    virtual ~RecordingRepository() = default;

    // ── The edge translation, and the folder as a property ──────────────────────────────────────
    // `swingDir`'s ShotRef: its session's id (SessionRepository::openSwingFolder — the session file's,
    // or for a session without one D3's, from `legacyAthlete`, else from the athlete who owns its folder,
    // the uuid the swing's own header records only when no owner is found: J-39; confirmed by Mark, 15 Nov 2026) and
    // its swing number. Null when it is not a swing folder (no swing number) or its
    // session cannot be named. Owner thread only.
    virtual ShotRef shotOfFolder(const QString &swingDir, const AthleteId &legacyAthlete = {}) = 0;
    // The shot's folder; "" when its session is not open. Any thread.
    virtual QString swingFolder(const ShotRef &shot) const = 0;

    virtual WriteResult create(const ShotRef &shot, const NewRecording &recording,
                               const PersistOptions &options = {}) = 0;
    // A shot ONLY a launch monitor saw: no streams, the raw reading, its rows and one Impact phase.
    virtual WriteResult create(const ShotRef &shot, const lm::LaunchMonitorReading &reading,
                               const DeviceOnlyMeta &meta) = 0;

    // A launch monitor reading folded into the shot (idempotent). `followedClub`, when not empty,
    // is the capture.club the device named (follow-club, IM-4): it replaces the recorded club only
    // when it names another, and never over a hand-set review.club; analysisContext.club (what
    // analysis reads) is rewritten with it (R-REC-2(a), DEC-82). *clubReplaced says whether it did.
    virtual WriteResult completeCapture(const ShotRef &shot, const lm::LaunchMonitorReading &reading,
                                        const QJsonObject &followedClub = {},
                                        bool *clubReplaced = nullptr) = 0;
    virtual WriteResult completeCapture(const ShotRef &shot, const StreamArrival &arrival) = 0;

    virtual WriteResult writeAnalysis(const ShotRef &shot, const AnalysisOutcome &outcome,
                                      const PersistOptions &options = {}) = 0;

    virtual WriteResult editReview(const ShotRef &shot, const ReviewEdit &edit) = 0;
    virtual ShotReview  readReview(const ShotRef &shot) = 0;
    // The shot's summary (above); ok = false when there is no document. `parseIfUnindexed` (readSwingSummary's
    // writeSidecar): true — a document with no current summary (a JSON-era swing without its sidecar, or a stale
    // one) is parsed whole and its sidecar written (FILE-SHOT-09; DEC-178 keeps that file until its decision), as
    // the carousel's session load has always done; false — it answers ok = false instead, for a GUI-thread path
    // that must never parse a whole document (the row of a shot just captured).
    virtual ShotSummary readSummary(const ShotRef &shot, bool parseIfUnindexed = true) = 0;
    // The shot's measurements (above). Any thread; the read is SwingStore's analysis read (6–9 ms a swing).
    virtual analysis::MeasurementSet readMeasurements(const ShotRef &shot) = 0;

    virtual ConversionResult convert(const ShotRef &shot, bool deleteJson) = 0;

    // `summary` is the shot as read back (its swingDir is provenance; the shot's folder is the key's).
    virtual WriteResult writeSummarySidecar(const ShotRef &shot, const PersistedShot &summary) = 0;

    // The archive's stub over the shot's document (FILE-SHOT-10): `stub` is the whole stub document
    // (the archiver thins it, session_archiver.h). A read-only document is refused like every write.
    virtual WriteResult writeArchiveStub(const ShotRef &shot, const QJsonObject &stub) = 0;

    // The markup truth (FILE-SHOT-12), written whole and atomically; `truth` reads it back ({} when the
    // shot has none, or it cannot be read).
    virtual WriteResult writeTruth(const ShotRef &shot, TruthView view, const QJsonObject &json) = 0;
    virtual QJsonObject truth(const ShotRef &shot, TruthView view) = 0;

    // A ONE-OFF MIGRATION (R-REC-1: the only other writes of a swing file are migrations Mark
    // approves one by one; this is DEC-34's). Writes the `analysisContext` block into a document
    // that has none, built from its legacy fields and `facts`. Written = written (on a dry run:
    // WOULD be); Unchanged = nothing to record — the document already has a block (capture recorded
    // it: never rewritten), or the facts add nothing to what its legacy context already reads;
    // NoDocument / ReadOnly as every write. PersistOptions::dryRun is honoured; savePose is ignored
    // (no analysis is written).
    virtual WriteResult backfillAnalysisContext(const ShotRef &shot, const AthleteRecordFacts &facts,
                                                const PersistOptions &options = {}) = 0;

    virtual bool isDeviceOnly(const ShotRef &shot) = 0;
};

} // namespace pinpoint
