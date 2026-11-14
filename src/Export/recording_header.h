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

// THE ONE HEADER WRITER of the swing document, and the `analysisContext` block's writer and
// reader (refactor step 1.2; docs/developer/information_model.md §2, IM-1; docs/reference/
// swing_json_schema.md).
//
// Before step 1.2 four builders wrote the top of a swing document and drifted: the exporter
// (no athlete.heightM, schema /1), the analysis-only manifest (heightM, "mirrors the exporter
// exactly"), the phone-clip placeholder (schema /1) and the device-only shot (session.id,
// window.startUs/endUs, its own blocks). Now every one of them fills a RecordingHeader and calls
// recordingHeaderJson(); the parts a caller does not know are left out, never invented.
//
//   schema           kSwingDocSchema — THE ONE SCHEMA CONSTANT ("pinpoint.swing/2"), read back by
//                    swingDocSchemaVersionOf(); a document newer than this build is read-only
//   swing            { index, id }
//   athlete          { name, uuid, handedness, heightM (when known) }
//   session          { dir }
//   clock            { t0_us, unit, wallclock }
//   window           { start_us, end_us }
//   thumbnail        { file, t_us }                 media: only beside the streams
//   capture          the capture facts (captureBlockJson for a camera / IMU shot)
//   analysisContext  the analysis context recorded (IM-1), schema-tagged
//   streams          the media streams (empty for an analysis-only or device-only shot)
//
// THE ANALYSIS CONTEXT ON DISK (IM-1, D2, D3; Mark 14 Nov 2026):
//
//   "analysisContext": {
//     "schema":  "pinpoint.analysisContext/1",
//     "athlete": { "handedness": "Right" | "Left" | "unknown", "heightM": 1.80 (0 = unknown),
//                  "clubSet": "men" | "women" | "junior" },
//     "club":    { "name": canonical id ("" = undeclared, never the stub), "lengthMm", "lengthKnown",
//                  "shaftType", "bandCentersMm", "hoselFromButtMm", "shaftLengthMm", "handsEndMm",
//                  "standardLengthMm", "lengthPrior": { "px", "varPx", "n" } (only when present) },
//     "bag":     { "<canonical id>": { the same seven geometry keys }, … }
//   }
//
// No sex, no date of birth, no age band: the cohort recorded is the club set (D3, IM-7).
//
// A DOCUMENT WITHOUT THE BLOCK (everything before step 1.2) gets the LEGACY context, built once in
// memory by readAnalysisContext() and never written back by reading:
//   handedness / heightM   athlete.handedness / athlete.heightM (0 when absent)
//   clubSet                athlete.sex / athlete.dob on the session's date, as today's re-analysis
//                          read them (no writer ever put them in a document, so in practice men's)
//   club                   capture.club: the TRIMMED name as stored, its geometry and prior
//   standardLengthMm       standardClub(the trimmed captured name, club set, height), set only when
//                          that name is a standard club (today's st.valid gate). The DECLARED club
//                          (review.club) is not folded in here: it reaches effectiveContext() as
//                          ClubReview::declared, which applies it exactly as today's re-analysis
//                          did (HANDOFF from 1.1's review).
//   bag                    empty (no bag was recorded)
//
// Adapters ring (src/Export): JSON in and out, no file I/O.

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <optional>

#include "../Recording/analysis_context.h"
#include "../Analysis/effective_context.h"   // pinpoint::analysis::ClubReview

namespace pinpoint {

struct SwingExportJob;

// ── The one schema constant ───────────────────────────────────────────────────────────────────
inline constexpr int kSwingDocSchemaVersion = 2;
inline constexpr const char *kSwingDocSchema = "pinpoint.swing/2";
// The version a document says it is ("pinpoint.swing/<n>" → n); 0 when it says nothing readable.
int swingDocSchemaVersionOf(const QJsonObject &root);

inline constexpr int kAnalysisContextSchemaVersion = 1;
inline constexpr const char *kAnalysisContextSchema = "pinpoint.analysisContext/1";

// ── The header ────────────────────────────────────────────────────────────────────────────────
// Every part a caller does not know stays unset and is not written.
struct RecordingHeader {
    struct Identity { QString id; int index = 0; };
    struct Athlete {
        QString name, uuid;
        std::optional<QString> handedness;   // the athlete record's spelling; unset = not written
        double heightM = 0.0;                 // written only when > 0
    };
    struct Clock { int64_t t0Us = 0; QDateTime wallclock; };
    struct Window { int64_t startUs = 0; int64_t endUs = 0; };
    struct Thumbnail { QString file; int64_t tUs = -1; };

    std::optional<Identity>  swing;
    std::optional<Athlete>   athlete;
    std::optional<QString>   sessionDir;
    std::optional<Clock>     clock;
    std::optional<Window>    window;
    std::optional<Thumbnail> thumbnail;
    QJsonObject              capture;        // empty = not written
    std::optional<recording::AnalysisContext> analysisContext;
};

// THE header writer: the document's top-level blocks, in the order above, with `streams` last.
QJsonObject recordingHeaderJson(const RecordingHeader &header, const QJsonArray &streams);

// The header of a camera / IMU shot, from the export job: what the exporter writes beside its
// streams and the analysis-only shot writes with none — the same header either way. `t0Us` and
// `durationUs` are the window's start (ring clock) and length; the wallclock is the job's anchor
// minus the duration (the window START).
RecordingHeader recordingHeaderFor(const SwingExportJob &job, int64_t t0Us, int64_t durationUs);

// The top-level "capture" block (session context + host provenance + capture.club, kept for
// provenance and older readers) from the export job's value fields.
QJsonObject captureBlockJson(const SwingExportJob &job);

// ── The analysis context: capture side ────────────────────────────────────────────────────────
// A bag record (AthleteController::clubsFor's per-club map: lengthMm, shaftType, bandCentersMm,
// hoselFromButtMm, shaftLengthMm, handsEndMm) → its geometry. lengthKnown = lengthMm > 0.
recording::ClubGeometry clubGeometryFromRecord(const QVariantMap &record);

// The context capture records with a shot: the export job's athlete and club (the club the session
// was on, its geometry and the length prior the live fuse used), the club set on `on` (the swing's
// date) from the athlete's sex and date of birth — used here and NOT recorded (IM-7) — and the bag
// (club id → record, canonicalised). The standard length is the captured club's for that set and
// height, 0 when it names no standard club.
recording::AnalysisContext captureAnalysisContext(const SwingExportJob &job, const QString &sex,
                                                  const QString &dobIso, const QDate &on,
                                                  const QVariantMap &bagRecords);

// ── The analysis context: on disk ─────────────────────────────────────────────────────────────
QJsonObject analysisContextJson(const recording::AnalysisContext &context);
// The block's fields (unknown keys ignored). The schema is checked by readAnalysisContext.
recording::AnalysisContext analysisContextFromJson(const QJsonObject &block);

// The context for a document with no block: built from its legacy fields (see the file header).
recording::AnalysisContext legacyAnalysisContext(const QJsonObject &root);

struct AnalysisContextRead {
    recording::AnalysisContext context;
    bool    recorded = false;     // true = the document's own block; false = the legacy context
    // The document, or its block, is newer than this build understands: read what is known, write
    // nothing back (a re-analysis refuses, so the newer document is never overwritten).
    bool    readOnly = false;
    QString readOnlyReason;
};
// The one reader: the block when present (schema-checked), else the legacy context; the document's
// own schema checked against kSwingDocSchemaVersion.
AnalysisContextRead readAnalysisContext(const QJsonObject &root);

// The golfer's two club edits on the shot's review block, as stored, for effectiveContext():
// correction = review.clubOverride (the LM board's), declared = review.club (the popover's).
analysis::ClubReview clubReviewOf(const QJsonObject &root);

} // namespace pinpoint
