/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#pragma once

// TEST HELPER (refactor step 5.1): a ShotCommandDoor over a test's swing folders, for the tests of the carousel and the
// phone-clip filer — the views — that are not about the Session. Since step 5.1 those adapters keep no read, write or
// trash of their own (N1 is gone: every session is a Session, the Unassigned owner's with no golfer); they reach a shot
// only through the door. This door stands where `app` stands in the app: each command goes straight to the one swing
// door (testdoc::repo(), the repository the app's Shots use), so the tests keep testing what they always tested — the
// rows, the numbering, the edits round-tripping, the trash's release and retry — through the adapter's real code.
// It is NOT the domain: the Session-level behaviour (events, the number kept, the Unassigned owner) is tested by
// shot_commands_test and athlete_repository_test against the real App.

#include "../../App/shot_command_door.h"
#include "../../Export/swing_doc.h"
#include "../../Export/swing_paths.h"
#include "../../Export/tests/swing_doc_test_writes.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <functional>

namespace pinpoint::testdoc {

class FolderDoor final : public ShotCommandDoor
{
public:
    // The trash's move (the retry tests hold it); default: the app's move (the library's own trash).
    std::function<bool(const QString &swingDir)> trashMove;
    int completeCaptures = 0;

    ForwardResult rate(const QString &, const ShotRef &s, int stars) override
    { ReviewEdit e; e.rating = stars; return edit(s, e); }
    ForwardResult note(const QString &, const ShotRef &s, const QString &text) override
    { ReviewEdit e; e.note = text; return edit(s, e); }
    ForwardResult setDeclaredClub(const QString &, const ShotRef &s, const QString &club) override
    { ReviewEdit e; e.club = club; return edit(s, e); }
    ForwardResult correctClub(const QString &, const ShotRef &s, const QString &club) override
    { ReviewEdit e; e.clubOverride = club; return edit(s, e); }
    ForwardResult excludeReading(const QString &, const ShotRef &s, bool excluded) override
    { ReviewEdit e; e.excluded = excluded; return edit(s, e); }
    ForwardResult completeCapture(const QString &, const ShotRef &, const lm::LaunchMonitorReading &, const QJsonObject &,
                                  bool *) override { return {}; }
    ForwardResult completeCapture(const QString &, const ShotRef &s, const StreamArrival &arrival) override
    {
        if (s.isNull()) return {};
        ++completeCaptures;
        return { true, repo().completeCapture(s, arrival) };
    }
    ForwardResult setMarkupTruth(const QString &, const ShotRef &, TruthView, const QJsonObject &) override { return {}; }
    bool setShotIntent(const QString &, const ShotRef &, const QString &, bool, const SaveLater &) override { return false; }
    bool correctLabel(const QString &, const ShotRef &, const QVector<LabelCorrection> &, const LabelCorrectionNote &, bool,
                      const SaveLater &) override { return false; }
    ForwardResult flagForTuning(const QString &, const ShotRef &, const TuningFlagNote &) override { return {}; }
    ForwardResult setSessionIntent(const QString &, const SessionIntentEdit &, const SessionInput &) override { return {}; }
    ForwardResult setIntentRun(const QString &, const QVector<IntentRun> &, const SessionInput &) override { return {}; }

    std::optional<ShotSummary> shotSummary(const QString &, const ShotRef &s, SummaryRead) override
    {
        if (s.isNull() || repo().swingFolder(s).isEmpty()) return std::nullopt;
        return repo().readSummary(s);   // as the carousel's own read was: the summary, else the document parsed
    }
    std::optional<QVector<HeldShot>> sessionShots(const QString &sessionFolder) override
    {
        if (!QFileInfo(sessionFolder).isDir()) return std::nullopt;
        QVector<HeldShot> out;
        for (const QString &sd : SwingDocReader::findSwingDirs(sessionFolder)) {
            const ShotRef s = repo().shotOfFolder(sd);
            const ShotSummary sum = repo().readSummary(s);
            if (sum.ok) out.append(HeldShot{ s, sd, sum });
        }
        std::sort(out.begin(), out.end(), [](const HeldShot &a, const HeldShot &b) {
            return a.summary.ordinal < b.summary.ordinal;
        });
        return out;
    }
    ForwardResult trashShot(const QString &, const ShotRef &s, QString *) override
    {
        const QString dir = repo().swingFolder(s);
        if (dir.isEmpty()) return {};
        if (!QFileInfo::exists(dir)) return { true, { WriteResult::Status::Unchanged, {} } };   // gone counts as gone
        const bool moved = trashMove ? trashMove(dir) : SwingPaths::trashPath(dir);
        return { true, moved ? WriteResult{ WriteResult::Status::Written, {} }
                             : WriteResult{ WriteResult::Status::Failed, QStringLiteral("refused") } };
    }
    std::optional<RecordingSource> recordingSourceOf(const QString &, const ShotRef &) override { return std::nullopt; }
    ForwardResult applyAnalysis(const QString &, const ShotRef &s, const AnalysisOutcome &outcome,
                                const PersistOptions &options) override
    { return s.isNull() ? ForwardResult{} : ForwardResult{ true, repo().writeAnalysis(s, outcome, options) }; }
    ShotRef shotOfFolder(const QString &swingFolder) override { return repo().shotOfFolder(swingFolder); }
    std::optional<bool> isDeviceOnly(const QString &, const ShotRef &s) override
    { return s.isNull() ? std::nullopt : std::optional<bool>(repo().isDeviceOnly(s)); }

private:
    static ForwardResult edit(const ShotRef &s, const ReviewEdit &e)
    { return s.isNull() ? ForwardResult{} : ForwardResult{ true, repo().editReview(s, e) }; }
};

} // namespace pinpoint::testdoc
