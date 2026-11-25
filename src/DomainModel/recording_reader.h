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

// RecordingReader — READING A SHOT'S RECORDING BACK (refactor step 1.5; docs/developer/
// information_model.md §2 "The Recording contract"; step 1 design §4.8 `load`).
//
//   load(ShotRef) → { the disk-backed window, the Recording that points into it,
//                     the shot's club review (ClubReview { correction, declared }), … }
//
// KEYED BY THE SHOT'S ShotRef (step 2.4; IM-S2-2: DEC-160), like RecordingRepository: the folder is
// resolved through SessionRepository and is never the key. A caller holding a path translates it once,
// at its edge (RecordingRepository::shotOfFolder; DEC-90).
//
// A SEPARATE, SMALL INTERFACE, NOT A METHOD OF RecordingRepository (the lead's call, 14 Nov 2026,
// from 1.4's notes): its adapter decodes frames (OpenCV, the MP4 reader), and a `load` on the
// repository would pull that into every writer of a review. So the write door stays light, and the
// one reader of Recordings — the analysis side (re-analysis, SwingLab) — names this interface.
//
// Its adapter is SwingStoreRecordingReader (src/Export/recording_loader.h), which the composition
// root holds (each tool's main; the app's re-analysis). Reading never writes the document. Stateless
// and safe from worker threads.
//
// Domain model ring (src/DomainModel): names no store type. The values it returns are the
// Recording contract's (src/Vocabulary/Recording) and the effective-club rule's input (ClubReview).

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "../Vocabulary/Identity/identity.h"
#include "../Vocabulary/Recording/recording.h"
#include "../Analysis/effective_context.h"   // pinpoint::analysis::ClubReview
#include "../Vocabulary/Analysis/gate_record.h"         // loadGates
#include "../Vocabulary/Common/sidecar_policy.h"                     // SidecarPolicy (phaseGrid)
#include "../Vocabulary/Analysis/phase_grid.h"          // SwingPhaseGrid, PhaseGridConfig (phaseGrid): value types only

namespace pinpoint {

class SwingWindow;

struct RecordingLoadOptions {
    // Which camera is face-on: by default the recorded setup.perspective; SwingLab's --face-on
    // forces a substring match on the alias / file name (an escape hatch for mislabelled
    // recordings). Legacy streams without "setup" use the substring too.
    QString faceOnSubstring = QStringLiteral("Face");
    bool    faceOnExplicit  = false;
    // Only the decode.* keys are read here (the MP4 readers' threads / hwAccel / seek / backend).
    QVariantMap tuningOverrides;
};

struct LoadedRecording {
    bool    ok = false;
    QString error;

    // The disk-backed window the Recording's streams are read from. Owned here; recording.window
    // points into it, so keep this alive (and do not move the window out) while the Recording is
    // in use.
    std::unique_ptr<pinpoint::SwingWindow> window;
    pinpoint::recording::Recording         recording;

    // The shot's review facts, as stored, for effectiveContext(): the board's override and the
    // popover's club. Analysis never reads the review block itself.
    pinpoint::analysis::ClubReview review;

    // The analysis context: true = the document's own analysisContext block; false = the legacy
    // context built from its older fields (recording_header.h).
    bool    contextRecorded = false;
    // The document (or its block) is newer than this build understands: read what is known, never
    // write it back.
    bool    readOnly = false;
    QString readOnlyReason;

    QJsonObject analysisIn;          // the recorded analysis block (version-gated reuse)
    // The origin the reuse readers take times in: analysisReadOriginUs(root) (swing_doc.h) — 0 for
    // a document stamped timeBase "window", else its clock.t0_us.
    int64_t analysisOriginUs = 0;
    bool    usedRaw = false;         // any camera used the raw sidecar
    // Gate records decided while LOADING (trust stage 6): a HackMotion lane that could not be
    // bound (hm_binding_recon.h refusalRecords). The analysis appends them to its own.
    std::vector<pinpoint::analysis::GateRecord> loadGates;

    // ── What the Recording records about itself, for the views that show it (refactor 5.8a) ─────────
    // Read accessors on the Recording's facts (the Solution Architect, 23 Nov 2026: a view reads each block from its
    // owner): the document's header facts and each stored stream's own description, as the swing file holds them. The
    // analysis never reads these. Filled by the one loader from the read it already makes; nothing is read twice.
    //
    // documentRead: the document itself was read — true even when `ok` is false because the shot has no camera stream
    // (a device-only shot), so a view can still show its analysis, its header and its streams' descriptions.
    struct StoredStream {
        QString     kind;                 // "video" | "imu" | "ball" | …, as stored
        QString     alias;
        QJsonObject source, device, setup, capture, encoded;   // the stream's own blocks, as stored ({} when absent)
        bool        hasSampleCount = false;            // samples.count is stored
        int         sampleCount = 0;
        // The stream's lane in `window` (an IMU lane read into it); kInvalidSourceId otherwise.
        pinpoint::SourceId lane = pinpoint::kInvalidSourceId;
    };
    bool                      documentRead = false;
    bool                      hasSchema = false;      // the root's `schema`
    QString                   schema;
    bool                      hasWallclock = false;   // clock.wallclock: when the shot was captured
    QString                   wallclock;
    QJsonObject               captureIn;              // the root's `capture` block (the shot's capture settings and host)
    std::vector<StoredStream> storedStreams;          // every stored stream, in the document's order
};

class RecordingReader {
public:
    virtual ~RecordingReader() = default;
    // The Recording of the shot and its club review. On failure (no document, a shot whose session is
    // not open) ok=false, error set, no window.
    virtual LoadedRecording load(const ShotRef &shot, const RecordingLoadOptions &options = {}) = 0;

    // THE SHOT'S PHASE GRID (FILE-SHOT-07, OBJ-PHASE-GRID; platform refactor P-3, ruled 24 Nov 2026, J-104): the sampler's
    // numbers per phase, its cache read and written by the adapter through the swing store's file door (PhaseGridStore,
    // src/Export/phase_grid_store.h), never by the caller. The grid is built under `config` (its millisecond spans
    // widened to what the cache already holds); `policy` (src/Vocabulary/Common/sidecar_policy.h) says what a miss or a stale cache may
    // cost: CachedOnly never parses, ParseNoWrite parses and writes nothing, ParseAndWrite parses and indexes the result
    // under the session's lock and outside the archive window. buildPhaseGrid stays a pure function. Any thread.
    //
    // nullopt: this reader has no phase-grid door (a test double, the base store adapter), or the shot's session is not
    // open; an EMPTY grid: the shot has no document, or nothing readable under `policy` (CachedOnly on a miss).
    virtual std::optional<analysis::SwingPhaseGrid> phaseGrid(const ShotRef &shot, const analysis::PhaseGridConfig &config = {},
                                                              SidecarPolicy policy = SidecarPolicy::ParseAndWrite)
    {
        Q_UNUSED(shot) Q_UNUSED(config) Q_UNUSED(policy)
        return std::nullopt;
    }
};

} // namespace pinpoint
