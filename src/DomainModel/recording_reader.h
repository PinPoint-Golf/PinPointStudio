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
//   load(swingDir) → { the disk-backed window, the Recording that points into it,
//                      the shot's club review (ClubReview { correction, declared }), … }
//
// A SEPARATE, SMALL INTERFACE, NOT A METHOD OF RecordingRepository (the lead's call, 14 Nov 2026,
// from 1.4's notes): its adapter decodes frames (OpenCV, the MP4 reader), and a `load` on the
// repository would pull that into every writer of a review. So the write door stays light, and the
// one reader of Recordings — the analysis side (re-analysis, SwingLab) — names this interface.
//
// Its adapter is SwingStoreRecordingReader (src/Export/recording_loader.h), which the composition
// root holds (each tool's main; the app when step 3's Session reads through it). Reading never
// writes the document. Stateless and safe from worker threads.
//
// Domain model ring (src/DomainModel): names no store type. The values it returns are the
// Recording contract's (src/Recording) and the effective-club rule's input (ClubReview).

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <vector>

#include "../Recording/recording.h"
#include "../Analysis/effective_context.h"   // pinpoint::analysis::ClubReview
#include "../Analysis/gate_record.h"         // loadGates

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
};

class RecordingReader {
public:
    virtual ~RecordingReader() = default;
    // The Recording of the swing in `swingDir` and the shot's club review. On failure ok=false,
    // error set, no window.
    virtual LoadedRecording load(const QString &swingDir, const RecordingLoadOptions &options = {}) = 0;
};

} // namespace pinpoint
