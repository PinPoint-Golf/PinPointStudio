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
// Every write of a shot's `swing.ppsw` goes through this interface. The swing folder is the key
// until `ShotRef` (step 2). SwingStoreRecordingRepository (src/Export/swing_store_repository.h)
// implements it; the composition root (main.cpp, each tool's main) holds the one instance and
// hands it to the callers, so a caller names this interface and never the store.
//
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
//                     exclusion, club override)
//   convert           a JSON-era swing → swing.ppsw, proven before the JSON is deleted
//   writeSummarySidecar  the JSON-era swing_summary.json cache
//   isDeviceOnly      a read: did the launch monitor alone record this shot?
//
// ⛔ EVERY WRITE REFUSES A READ-ONLY DOCUMENT: one saved by a newer PinPoint Studio (its schema, or
// its analysisContext block's, is newer than this build reads). Such a file is never rewritten
// (WriteResult::Status::ReadOnly), so a newer build's fields are never lost to an older one.
//
// INTERIM (IM-5; Mark, 14 Nov 2026): until step 3 the callers are today's — ShotListModel,
// ReanalysisController, the launch monitor controller, the phone-clip filer, the archiver, the
// library converter and the tools — all through this interface; the `Session` takes them over in
// step 3. The live join (ShotProcessor) writes through create() since step 1.5. Reading a Recording
// back is NOT here: it is the separate, small RecordingReader (recording_reader.h, step 1.5), so this
// write door does not pull frame decoding into every writer of a review.
//
// Stateless and safe from worker threads, as the writer underneath is.
//
// Domain model ring (src/DomainModel): names no store type. The analysis and the launch monitor
// reading are passed by reference to their own (inner-ring) types, declared forward here.

#include <QJsonObject>
#include <QString>
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
        Failed,       // the arguments were refused, or the write itself failed
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

// ── editReview ───────────────────────────────────────────────────────────────────────────────────
// The golfer's edits, MERGED into the review block: an unset field is left as it is, so a star
// rating never un-excludes a shot. A rating, note or club edit always rewrites (and so re-indexes)
// the document; an exclusion or override edit writes only when it changes something, and removes
// its key when set back to the default (false / ""), so absence keeps meaning "never annotated".
struct ReviewEdit {
    std::optional<int>     rating;        // 0–5, clamped
    std::optional<QString> note;
    std::optional<QString> club;          // the popover's (declared) club
    std::optional<bool>    excluded;      // left out of the launch monitor board's statistics
    std::optional<QString> clubOverride;  // the board's club for the shot ("" = the default)
};

// ── convert ──────────────────────────────────────────────────────────────────────────────────────
struct ConversionResult {
    enum class Outcome { Converted, AlreadyConverted, NoDocument, Failed };
    Outcome outcome   = Outcome::Failed;
    qint64  jsonBytes = 0;
    qint64  ppswBytes = 0;
    QString error;     // Failed: what failed (a newer document is refused here too)
};

class RecordingRepository {
public:
    virtual ~RecordingRepository() = default;

    virtual WriteResult create(const QString &swingDir, const NewRecording &recording,
                               const PersistOptions &options = {}) = 0;
    // A shot ONLY a launch monitor saw: no streams, the raw reading, its rows and one Impact phase.
    virtual WriteResult create(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                               const DeviceOnlyMeta &meta) = 0;

    // A launch monitor reading folded into the shot (idempotent). `followedClub`, when not empty,
    // is the capture.club the device named (follow-club, IM-4): it replaces the recorded club only
    // when it names another, and never over a hand-set review.club. *clubReplaced says whether it did.
    virtual WriteResult completeCapture(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                        const QJsonObject &followedClub = {},
                                        bool *clubReplaced = nullptr) = 0;
    virtual WriteResult completeCapture(const QString &swingDir, const StreamArrival &arrival) = 0;

    virtual WriteResult writeAnalysis(const QString &swingDir, const AnalysisOutcome &outcome,
                                      const PersistOptions &options = {}) = 0;

    virtual WriteResult editReview(const QString &swingDir, const ReviewEdit &edit) = 0;

    virtual ConversionResult convert(const QString &swingDir, bool deleteJson) = 0;

    virtual WriteResult writeSummarySidecar(const PersistedShot &shot) = 0;

    virtual bool isDeviceOnly(const QString &swingDir) = 0;
};

} // namespace pinpoint
