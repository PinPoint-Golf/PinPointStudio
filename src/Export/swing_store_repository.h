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
// the swing store: the only code that writes a swing file, its archive stub and its markup truth
// (refactor step 1.4, keyed by ShotRef since step 2.4; information_model.md §8). SwingDocWriter's
// writes are private and this is their friend; SwingStore does the bytes.
//
// Created by the composition root — main.cpp, and each tool's main (lm_repair, pps_convert_library,
// swinglab_run, backfill_analysis_context), the archiver and the library converter for their own runs
// — over THE SessionRepository that names the shots' folders (the app's one SessionStore; a tool's
// own). Stateless apart from the writer's one-entry just-written cache (H-13, unchanged), so one
// instance serves every thread; it is cheap enough that a test makes its own.
//
// What each method does on top of the writer:
//   every method      resolves the shot's folder through SessionRepository::swingFolder (any thread);
//                     a shot it cannot resolve is Failed / empty, never guessed at
//   shotOfFolder      SessionRepository::openSwingFolder, and for a session without a session file and
//                     no athlete from the caller, the athlete who OWNS its folder (J-39, proposed, for
//                     Mark): setOwnerOf's resolver, else the folder's athlete.json; the swing's own
//                     `athlete.uuid` (its root alone) only when no owner is found
//   create            refuses to overwrite a document a newer build saved (when one is there)
//   writeAnalysis     the re-analysis write-back: reads the document (the empty-document guard),
//                     sets both integrity verdicts on it (nullptr withdraws one), writes the
//                     analysis with PersistOptions::savePose; a launch-monitor-only shot has its
//                     launch monitor rows derived again instead
//   convert           SwingStore's verified conversion, in the domain's terms
//   writeArchiveStub  the stub as given (no summary rebuild), refused for a read-only document
//   writeTruth/truth  truth.json / truth_dtl.json, written whole through atomicWrite (src/Core)
//   backfillAnalysisContext  the step-1 backfill (DEC-34): the writer's recordBackfilledContext
//
// A TOOL'S OWN OUTPUT is not a shot: writeOutsideLibrary writes a document into a folder that is no
// session's (SwingLab's research outputs), by path, and is not on the interface.
//
// Adapters ring (src/Export).

#include "../DomainModel/recording_repository.h"

#include <functional>

namespace pinpoint {

class SessionRepository;

class SwingStoreRecordingRepository final : public RecordingRepository {
public:
    explicit SwingStoreRecordingRepository(SessionRepository &sessions) : m_sessions(&sessions) {}

    // THE OWNER OF A LEGACY SESSION (J-39, proposed, for Mark): D3's id is derived from the athlete who
    // OWNS the session's folder. shotOfFolder asks, in order: the caller's athlete; this resolver (the
    // app's: the current athlete, when the folder is theirs — the athlete the app's session list derives
    // the same id from); the athlete folder's athlete.json (AthleteFiles::ownerOf); and only then the
    // athlete the swing's own header records. Owner thread, like shotOfFolder.
    using OwnerOf = std::function<AthleteId(const QString &athleteFolder)>;
    void setOwnerOf(OwnerOf ownerOf) { m_ownerOf = std::move(ownerOf); }
    // That chain for an athlete folder (shotOfFolder's, without the caller's athlete): the resolver, then
    // athlete.json, then the athlete `swingDir`'s header records (skipped when it is empty). Null when none
    // does. For a caller that holds the athlete's folder and lists its sessions (SessionStore::enumerateFolder;
    // step 2.7a), which names one of its swings.
    AthleteId legacyOwnerOf(const QString &athleteFolder, const QString &swingDir = {}) const;

    // The markup truth's file names (FILE-SHOT-12): "truth.json" (face-on), "truth_dtl.json".
    static QString truthFileName(TruthView view);

    // NOT A SHOT: a document a tool writes into its own output folder (SwingLab's result and
    // --doc-blocks scratch), through the same writer, so it is exactly what a library write would
    // store. Never a library folder.
    static WriteResult writeOutsideLibrary(const QString &folder, const NewRecording &recording,
                                           const PersistOptions &options = {});

    ShotRef shotOfFolder(const QString &swingDir, const AthleteId &legacyAthlete = {}) override;
    QString swingFolder(const ShotRef &shot) const override;

    WriteResult create(const ShotRef &shot, const NewRecording &recording,
                       const PersistOptions &options = {}) override;
    WriteResult create(const ShotRef &shot, const lm::LaunchMonitorReading &reading,
                       const DeviceOnlyMeta &meta) override;

    WriteResult completeCapture(const ShotRef &shot, const lm::LaunchMonitorReading &reading,
                                const QJsonObject &followedClub = {},
                                bool *clubReplaced = nullptr) override;
    WriteResult completeCapture(const ShotRef &shot, const StreamArrival &arrival) override;

    WriteResult writeAnalysis(const ShotRef &shot, const AnalysisOutcome &outcome,
                              const PersistOptions &options = {}) override;

    WriteResult editReview(const ShotRef &shot, const ReviewEdit &edit) override;
    ShotReview  readReview(const ShotRef &shot) override;

    ConversionResult convert(const ShotRef &shot, bool deleteJson) override;

    WriteResult writeSummarySidecar(const ShotRef &shot, const PersistedShot &summary) override;

    WriteResult writeArchiveStub(const ShotRef &shot, const QJsonObject &stub) override;

    WriteResult writeTruth(const ShotRef &shot, TruthView view, const QJsonObject &json) override;
    QJsonObject truth(const ShotRef &shot, TruthView view) override;

    WriteResult backfillAnalysisContext(const ShotRef &shot, const AthleteRecordFacts &facts,
                                        const PersistOptions &options = {}) override;

    bool isDeviceOnly(const ShotRef &shot) override;

private:
    // Each write and read on the folder the ShotRef resolved to: the bodies of the forms above.
    WriteResult create(const QString &swingDir, const NewRecording &recording,
                       const PersistOptions &options);
    WriteResult create(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                       const DeviceOnlyMeta &meta);
    WriteResult completeCapture(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                const QJsonObject &followedClub, bool *clubReplaced);
    WriteResult completeCapture(const QString &swingDir, const StreamArrival &arrival);
    WriteResult writeAnalysis(const QString &swingDir, const AnalysisOutcome &outcome,
                              const PersistOptions &options);
    WriteResult editReview(const QString &swingDir, const ReviewEdit &edit);
    ConversionResult convert(const QString &swingDir, bool deleteJson);
    WriteResult writeSummarySidecar(const PersistedShot &shot);
    WriteResult backfillAnalysisContext(const QString &swingDir, const AthleteRecordFacts &facts,
                                        const PersistOptions &options);
    bool isDeviceOnly(const QString &swingDir);

    // The shot's folder, or "" with *why set.
    QString folderOf(const ShotRef &shot, QString *why) const;

    SessionRepository *m_sessions;
    OwnerOf            m_ownerOf;
};

} // namespace pinpoint
