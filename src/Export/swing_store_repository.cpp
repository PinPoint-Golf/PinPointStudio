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

#include "recording_header.h"   // readShotKind
#include "swing_doc.h"
#include "swing_store.h"

#include "athlete_files.h"      // AthleteFiles::ownerOf: a legacy session's owner (J-39)

#include "../Core/atomic_write.h"
#include "../DomainModel/session_repository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

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

// ── The shot's folder ────────────────────────────────────────────────────────────────────────────

QString SwingStoreRecordingRepository::folderOf(const ShotRef &shot, QString *why) const
{
    if (shot.isNull()) {
        *why = QStringLiteral("no shot was named (its folder is not a swing of a known session)");
        return {};
    }
    const QString dir = m_sessions->swingFolder(shot);
    if (dir.isEmpty())
        *why = QStringLiteral("the shot %1 was not found: its session is not open").arg(shot.toString());
    return dir;
}

ShotRef SwingStoreRecordingRepository::shotOfFolder(const QString &swingDir, const AthleteId &legacyAthlete)
{
    const ShotRef ref = m_sessions->openSwingFolder(swingDir, legacyAthlete);
    if (!ref.isNull() || !legacyAthlete.isNull() || swingDir.isEmpty())
        return ref;
    // A session without a session file (or not held), and no athlete from the caller: D3 names it from
    // the athlete who OWNS its folder (J-39, proposed, for Mark) — the resolver's (the app's current
    // athlete), else the athlete folder's athlete.json — and only when no owner can be found, from the
    // athlete the swing records it was captured for (`athlete.uuid`, the document's root alone). The
    // swings' uuids were minted on the machine that captured them and need not be the owner's (H-09).
    const QString athleteDir = QFileInfo(QFileInfo(QDir::cleanPath(swingDir)).path()).path();
    AthleteId owner = m_ownerOf ? m_ownerOf(athleteDir) : AthleteId();
    if (owner.isNull())
        owner = AthleteFiles::ownerOf(athleteDir);
    if (owner.isNull())
        owner = AthleteId::fromString(SwingStore::loadParts(swingDir, {}).value(QStringLiteral("athlete")).toObject()
                                          .value(QStringLiteral("uuid")).toString());
    return owner.isNull() ? ShotRef() : m_sessions->openSwingFolder(swingDir, owner);
}

QString SwingStoreRecordingRepository::swingFolder(const ShotRef &shot) const
{
    return shot.isNull() ? QString() : m_sessions->swingFolder(shot);
}

// ── The writes ───────────────────────────────────────────────────────────────────────────────────

WriteResult SwingStoreRecordingRepository::create(const ShotRef &shot, const NewRecording &recording,
                                                  const PersistOptions &options)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return create(swingDir, recording, options);
}

WriteResult SwingStoreRecordingRepository::create(const QString &swingDir, const NewRecording &recording,
                                                  const PersistOptions &options)
{
    WriteResult refused;
    if (refusesExisting(swingDir, &refused))
        return refused;
    return SwingDocWriter::writeDocument(swingDir, recording.header, recording.analysis,
                                         recording.reviewClub, options.savePose);
}

WriteResult SwingStoreRecordingRepository::writeOutsideLibrary(const QString &folder, const NewRecording &recording,
                                                               const PersistOptions &options)
{
    if (folder.isEmpty())
        return status(WriteResult::Status::Failed, QStringLiteral("no output folder"));
    WriteResult refused;
    if (refusesExisting(folder, &refused))
        return refused;
    return SwingDocWriter::writeDocument(folder, recording.header, recording.analysis,
                                         recording.reviewClub, options.savePose);
}

WriteResult SwingStoreRecordingRepository::create(const ShotRef &shot,
                                                  const lm::LaunchMonitorReading &reading,
                                                  const DeviceOnlyMeta &meta)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return create(swingDir, reading, meta);
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

WriteResult SwingStoreRecordingRepository::completeCapture(const ShotRef &shot,
                                                           const lm::LaunchMonitorReading &reading,
                                                           const QJsonObject &followedClub,
                                                           bool *clubReplaced)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return completeCapture(swingDir, reading, followedClub, clubReplaced);
}

WriteResult SwingStoreRecordingRepository::completeCapture(const QString &swingDir,
                                                           const lm::LaunchMonitorReading &reading,
                                                           const QJsonObject &followedClub,
                                                           bool *clubReplaced)
{
    return SwingDocWriter::updateLaunchMonitor(swingDir, reading, followedClub, clubReplaced);
}

WriteResult SwingStoreRecordingRepository::completeCapture(const ShotRef &shot,
                                                           const StreamArrival &arrival)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return completeCapture(swingDir, arrival);
}

WriteResult SwingStoreRecordingRepository::completeCapture(const QString &swingDir,
                                                           const StreamArrival &arrival)
{
    return SwingDocWriter::updateStreamOrigin(swingDir, arrival);
}

WriteResult SwingStoreRecordingRepository::writeAnalysis(const ShotRef &shot,
                                                         const AnalysisOutcome &outcome,
                                                         const PersistOptions &options)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return writeAnalysis(swingDir, outcome, options);
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

WriteResult SwingStoreRecordingRepository::editReview(const ShotRef &shot, const ReviewEdit &edit)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return editReview(swingDir, edit);
}

WriteResult SwingStoreRecordingRepository::editReview(const QString &swingDir, const ReviewEdit &edit)
{
    return SwingDocWriter::editReview(swingDir, edit);
}

ShotReview SwingStoreRecordingRepository::readReview(const ShotRef &shot)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty()) {
        ShotReview none;
        none.error = why;
        return none;
    }
    return SwingDocWriter::readReview(swingDir);
}

ConversionResult SwingStoreRecordingRepository::convert(const ShotRef &shot, bool deleteJson)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty()) {
        ConversionResult r;
        r.error = why;
        return r;   // Failed
    }
    return convert(swingDir, deleteJson);
}

ConversionResult SwingStoreRecordingRepository::convert(const QString &swingDir, bool deleteJson)
{
    ConversionResult r;
    const SwingStore::ConvertResult c = SwingDocWriter::convertToPpsw(swingDir, deleteJson);
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

WriteResult SwingStoreRecordingRepository::writeSummarySidecar(const ShotRef &shot, const PersistedShot &summary)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    // A swing.ppsw carries its summary inside, written with the document: there is no sidecar.
    if (SwingStore::info(swingDir).format == SwingStore::Format::Ppsw)
        return status(WriteResult::Status::Unchanged);
    PersistedShot at = summary;
    at.swingDir = swingDir;   // the key's folder, whatever the summary was read under
    return writeSummarySidecar(at);
}

WriteResult SwingStoreRecordingRepository::writeSummarySidecar(const PersistedShot &shot)
{
    if (SwingStore::info(shot.swingDir).format == SwingStore::Format::Ppsw)
        return status(WriteResult::Status::Unchanged);
    QString err;
    if (!SwingDocReader::writeSwingSummary(shot, &err))
        return status(WriteResult::Status::Failed, err);
    return status(WriteResult::Status::Written);
}

WriteResult SwingStoreRecordingRepository::writeArchiveStub(const ShotRef &shot, const QJsonObject &stub)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return SwingDocWriter::writeArchiveStub(swingDir, stub);
}

// ── The markup truth (FILE-SHOT-12) ──────────────────────────────────────────────────────────────

QString SwingStoreRecordingRepository::truthFileName(TruthView view)
{
    return view == TruthView::DownTheLine ? QStringLiteral("truth_dtl.json") : QStringLiteral("truth.json");
}

WriteResult SwingStoreRecordingRepository::writeTruth(const ShotRef &shot, TruthView view, const QJsonObject &json)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    // Indented, as the markup tool always wrote it (SwingLab's label.py / score.py read it). Into the
    // swing folder as it is: a folder that has gone is not made again around a lone truth file.
    const AtomicWriteResult w = atomicWrite(swingDir + QLatin1Char('/') + truthFileName(view),
                                            QJsonDocument(json).toJson(QJsonDocument::Indented),
                                            AtomicWriteOptions::intoExistingFolder());
    return w.ok ? status(WriteResult::Status::Written)
                : status(WriteResult::Status::Failed, w.detail.isEmpty() ? w.error : w.error + QStringLiteral(" (") + w.detail + QLatin1Char(')'));
}

QJsonObject SwingStoreRecordingRepository::truth(const ShotRef &shot, TruthView view)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return {};
    QFile f(swingDir + QLatin1Char('/') + truthFileName(view));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    return pe.error == QJsonParseError::NoError && doc.isObject() ? doc.object() : QJsonObject();
}

// ── The one-off migration, and the shot kind ─────────────────────────────────────────────────────

WriteResult SwingStoreRecordingRepository::backfillAnalysisContext(const ShotRef &shot,
                                                                   const AthleteRecordFacts &facts,
                                                                   const PersistOptions &options)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return status(WriteResult::Status::Failed, why);
    return backfillAnalysisContext(swingDir, facts, options);
}

WriteResult SwingStoreRecordingRepository::backfillAnalysisContext(const QString &swingDir,
                                                                   const AthleteRecordFacts &facts,
                                                                   const PersistOptions &options)
{
    return SwingDocWriter::recordBackfilledContext(swingDir, facts, options.dryRun);
}

bool SwingStoreRecordingRepository::isDeviceOnly(const ShotRef &shot)
{
    QString why;
    const QString swingDir = folderOf(shot, &why);
    if (swingDir.isEmpty())
        return false;   // not found: not known to be device-only (every write would refuse it anyway)
    return isDeviceOnly(swingDir);
}

bool SwingStoreRecordingRepository::isDeviceOnly(const QString &swingDir)
{
    // The one classification (recording_header.h): the recorded shot kind, else, for a document
    // older than the field, today's content rule. No document reads as captured (not device-only).
    return readShotKind(SwingStore::load(swingDir)).kind == recording::ShotKind::DeviceOnly;
}

} // namespace pinpoint
