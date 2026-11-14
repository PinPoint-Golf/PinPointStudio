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

#include "swing_store_repository.h"

#include "swing_doc.h"
#include "swing_store.h"

namespace pinpoint {

namespace {

WriteResult status(WriteResult::Status s, const QString &error = QString())
{
    WriteResult r;
    r.status = s;
    r.error  = error;
    return r;
}

// A first write over a document that is already there (rare: every creator allocates a fresh
// folder, or checks first) still refuses one a newer build saved.
bool refusesExisting(const QString &swingDir, WriteResult *out)
{
    if (!SwingStore::hasDocument(swingDir))
        return false;
    const QJsonObject existing = SwingStore::load(swingDir);
    return !existing.isEmpty() && SwingDocWriter::refusesWrite(existing, out);
}

} // namespace

WriteResult SwingStoreRecordingRepository::create(const QString &swingDir, const NewRecording &recording,
                                                  const PersistOptions &options)
{
    WriteResult refused;
    if (refusesExisting(swingDir, &refused))
        return refused;
    return SwingDocWriter::writeDocument(swingDir, recording.header, recording.analysis,
                                         recording.reviewClub, options.savePose);
}

WriteResult SwingStoreRecordingRepository::create(const QString &swingDir,
                                                  const lm::LaunchMonitorReading &reading,
                                                  const DeviceOnlyMeta &meta)
{
    WriteResult refused;
    if (refusesExisting(swingDir, &refused))
        return refused;
    return SwingDocWriter::writeDeviceOnlySwing(swingDir, reading, meta);
}

WriteResult SwingStoreRecordingRepository::completeCapture(const QString &swingDir,
                                                           const lm::LaunchMonitorReading &reading,
                                                           const QJsonObject &followedClub,
                                                           bool *clubReplaced)
{
    return SwingDocWriter::updateLaunchMonitor(swingDir, reading, followedClub, clubReplaced);
}

WriteResult SwingStoreRecordingRepository::completeCapture(const QString &swingDir,
                                                           const StreamArrival &arrival)
{
    return SwingDocWriter::updateStreamOrigin(swingDir, arrival);
}

WriteResult SwingStoreRecordingRepository::writeAnalysis(const QString &swingDir,
                                                         const AnalysisOutcome &outcome,
                                                         const PersistOptions &options)
{
    // No new analysis: the analysis stays as it is and the launch monitor rows are derived again
    // from the recorded reading — what re-analysing a launch-monitor-only shot means.
    if (!outcome.analysis)
        return SwingDocWriter::refreshLaunchMonitorRows(swingDir, options.dryRun);

    // The write-back preserves everything the analysis does not own (capture, streams, review, the
    // launch monitor's block and rows): it is the document on disk with its analysis replaced.
    //
    // THE EMPTY-DOCUMENT GUARD: an empty read means the document went missing or could not be read
    // between the analysis and now (the folder was trashed mid-batch, or a transient failure).
    // Writing then would replace the whole document with only {schema, analysis}, destroying its
    // streams, capture and review. Nothing is written.
    QString err;
    QJsonObject manifest = SwingStore::load(swingDir, &err);
    if (manifest.isEmpty())
        return status(WriteResult::Status::NoDocument,
                      err.isEmpty() ? QStringLiteral("no swing document in %1").arg(swingDir) : err);
    WriteResult refused;
    if (SwingDocWriter::refusesWrite(manifest, &refused))
        return refused;
    if (options.dryRun)
        return status(WriteResult::Status::Written);   // WOULD be written

    // The integrity verdicts are the one manifest key that must NOT simply ride through: each is a
    // verdict about this recording that this pass has just re-reached (or found it cannot reach, in
    // which case nullptr removes the block). Carrying the capture-time block forward is what made a
    // false ⚠ survive every re-analysis (swing_doc.h).
    applyImuIntegrity(manifest, outcome.imuIntegrity);
    applyCaptureIntegrity(manifest, outcome.captureIntegrity);

    // No review club: the document's own review rides through untouched.
    return SwingDocWriter::writeDocument(swingDir, manifest, outcome.analysis, QString(),
                                         options.savePose);
}

WriteResult SwingStoreRecordingRepository::editReview(const QString &swingDir, const ReviewEdit &edit)
{
    return SwingDocWriter::editReview(swingDir, edit);
}

ConversionResult SwingStoreRecordingRepository::convert(const QString &swingDir, bool deleteJson)
{
    const SwingStore::ConvertResult c = SwingDocWriter::convertToPpsw(swingDir, deleteJson);
    ConversionResult r;
    switch (c.outcome) {
        case SwingStore::ConvertResult::Outcome::Converted:   r.outcome = ConversionResult::Outcome::Converted; break;
        case SwingStore::ConvertResult::Outcome::AlreadyPpsw: r.outcome = ConversionResult::Outcome::AlreadyConverted; break;
        case SwingStore::ConvertResult::Outcome::NoDocument:  r.outcome = ConversionResult::Outcome::NoDocument; break;
        case SwingStore::ConvertResult::Outcome::Failed:      r.outcome = ConversionResult::Outcome::Failed; break;
    }
    r.jsonBytes = c.jsonBytes;
    r.ppswBytes = c.ppswBytes;
    r.error     = c.error;
    return r;
}

WriteResult SwingStoreRecordingRepository::writeSummarySidecar(const PersistedShot &shot)
{
    // A swing.ppsw carries its summary inside, written with the document: there is no sidecar.
    if (SwingStore::info(shot.swingDir).format == SwingStore::Format::Ppsw)
        return status(WriteResult::Status::Unchanged);
    QString err;
    if (!SwingDocReader::writeSwingSummary(shot, &err))
        return status(WriteResult::Status::Failed, err);
    return status(WriteResult::Status::Written);
}

bool SwingStoreRecordingRepository::isDeviceOnly(const QString &swingDir)
{
    return SwingDocWriter::isDeviceOnlySwing(SwingStore::load(swingDir));
}

} // namespace pinpoint
