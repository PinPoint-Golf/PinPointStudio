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

// SwingStoreRecordingRepository — RecordingRepository (src/DomainModel/recording_repository.h) on
// the swing store: the only code that writes a swing file (refactor step 1.4; information_model.md
// §8). SwingDocWriter's writes are private and this is their friend; SwingStore does the bytes.
//
// Created by the composition root — main.cpp, and each tool's main (lm_repair,
// pps_convert_library, swinglab_run) — and handed to the callers as the interface. Stateless apart
// from the writer's one-entry just-written cache (H-13, unchanged), so one instance serves every
// thread; it is cheap enough that a test makes its own.
//
// What each method does on top of the writer:
//   create            refuses to overwrite a document a newer build saved (when one is there)
//   writeAnalysis     the re-analysis write-back: reads the document (the empty-document guard),
//                     sets both integrity verdicts on it (nullptr withdraws one), writes the
//                     analysis with PersistOptions::savePose; a launch-monitor-only shot has its
//                     launch monitor rows derived again instead
//   convert           SwingStore's verified conversion, in the domain's terms
//
// Adapters ring (src/Export).

#include "../DomainModel/recording_repository.h"

namespace pinpoint {

class SwingStoreRecordingRepository final : public RecordingRepository {
public:
    WriteResult create(const QString &swingDir, const NewRecording &recording,
                       const PersistOptions &options = {}) override;
    WriteResult create(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                       const DeviceOnlyMeta &meta) override;

    WriteResult completeCapture(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                const QJsonObject &followedClub = {},
                                bool *clubReplaced = nullptr) override;
    WriteResult completeCapture(const QString &swingDir, const StreamArrival &arrival) override;

    WriteResult writeAnalysis(const QString &swingDir, const AnalysisOutcome &outcome,
                              const PersistOptions &options = {}) override;

    WriteResult editReview(const QString &swingDir, const ReviewEdit &edit) override;

    ConversionResult convert(const QString &swingDir, bool deleteJson) override;

    WriteResult writeSummarySidecar(const PersistedShot &shot) override;

    bool isDeviceOnly(const QString &swingDir) override;
};

} // namespace pinpoint
