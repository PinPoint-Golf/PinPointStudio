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

// PhaseGridStore — THE FILE DOOR OF A SHOT'S PHASE GRID, <swing>/swing_phasegrid.json (FILE-SHOT-07; platform refactor
// P-3). The one place the sidecar is read and written (R-PERS-1: one door per file); the archiver also spells its name, to
// thin it with the other caches (session_archiver.cpp, isCache). Until P-3 this was the
// phase-grid sampler's own file work in src/Diagnostics/measures/measure_sample.cpp (a rules directory, lint A1 and A5);
// the sampler kept the pure half: building the grid from a document (buildPhaseGrid, phaseGridOfDocument) and the
// sidecar's format (savePhaseGrid, loadPhaseGrid).
//
// Keyed by the swing FOLDER. Its callers by the shot reach it through the Recording's reader
// (RecordingReader::phaseGrid, src/DomainModel/recording_reader.h; the app's adapter SwingStorePhaseGridReader,
// recording_loader.h) or the firing reader (firing_reader_store.h). Its one caller by folder is the model browser, which
// has no Shot to ask: its corridor scan (FILE-SHOT-07's recorded exception) and its gate count's fallback for a session not
// summarised yet reach this door through its own folder read (src/Gui/diagnosticmodel/corridor_grid_read.h readPhaseGrid,
// defined in model_browser.cpp), whose include of this header the architecture allowlist counts on model_browser.cpp's A5
// line (B1.14, R11). Tools and tests over bare folders call it directly.
//
// LIGHT ON PURPOSE: it needs only what the sampler needed before P-3 (the swing store, the atomic write, the club
// vocabulary, the wrist profile's input), because every target that compiles the sampler compiles this beside it. The
// session lock and the archive marker (the firing read's guarded write) stay in firing_reader_store.cpp.
//
// Adapters ring (src/Stores). Safe from any thread; it holds no state.

#include "../Diagnostics/measures/measure_sample.h"   // SwingPhaseGrid, PhaseGridConfig, savePhaseGrid, loadPhaseGrid
#include "../Vocabulary/Common/sidecar_policy.h"
#include "../Wrist/wrist_profile_assess.h"            // WristInput

#include <QString>
#include <QtGlobal>

namespace pinpoint::SwingStore { class LazyDocument; }   // swing_store.h

namespace pinpoint::PhaseGridStore {

// <swingDir>/swing_phasegrid.json.
QString sidecarPath(const QString &swingDir);

// One swing's grid, preferring the sidecar and falling back to a parse of the swing's document. `policy` says what a
// miss or a stale sidecar may cost (src/Vocabulary/Common/sidecar_policy.h): CachedOnly never parses (an unindexed swing reads as an
// empty grid, or the held grid without a missing millisecond span); ParseNoWrite parses and writes nothing;
// ParseAndWrite parses and indexes (best-effort, unlocked: the folder-keyed readers' write, as before P-3).
// `parsed`, when given, is the one parse a caller also reading the summary shares (issue #142); null parses on its own.
// The grid's swingDir is set on every path that returns a grid; the empty grid of a missing document has none.
analysis::SwingPhaseGrid read(const QString &swingDir, SidecarPolicy policy, const analysis::PhaseGridConfig &cfg = {},
                              SwingStore::LazyDocument *parsed = nullptr);

// The grid a measure source reads: read() with the millisecond spans `measures` name (analysis::msWindowsOf) added to
// `cfg`, its swingDir set even on the empty grid a missing document gives (a source that cannot say which swing it
// failed on is unloggable). The firing reader's read, and the Recording's reader's caller's (with the same spans).
analysis::SwingPhaseGrid readForMeasures(const QString &swingDir, const std::vector<analysis::Measure> &measures,
                                         SidecarPolicy policy, const analysis::PhaseGridConfig &cfg = {},
                                         SwingStore::LazyDocument *parsed = nullptr);

// Index `grid` under the document stamp it was built from (size, mtime): an atomic write into the swing folder, which
// must already be there (a trashed folder is not re-made). Best-effort: false when it could not be written.
bool write(const QString &swingDir, const analysis::SwingPhaseGrid &grid, qint64 sourceSize, qint64 sourceMtimeMs);

// THE SHOT'S WRIST READINGS for its wrist checkpoint profile (refactor 3.8-ii; DEC-123: fire_findings.h FiringReadings::
// wrist). Empty unless `grid` (the shot's phase grid, just read) carries a metric that names a wrist DOF; then the
// document's analysis metrics and phases — from `parsed` when the grid's read already parsed it, else the analysis
// alone (SwingStore::loadAnalysis: the root chunk and the chunks on the path to the metrics, never the tracks).
analysis::WristInput readWristInput(const QString &swingDir, const analysis::SwingPhaseGrid &grid,
                                    SwingStore::LazyDocument *parsed = nullptr);

} // namespace pinpoint::PhaseGridStore
