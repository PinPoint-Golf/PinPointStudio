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

// club_length_prior_test: the learned club-length prior (SET-05) reaches a shot's analysis unchanged across its move
// from the settings store to the Rig (platform refactor C-7). The capture side's read of the entry
// (club_length_prior_entry.h, the code capture_context.cpp runs at each freeze) is given the entry three ways: as the
// settings store held it before C-7 (written by today's open-coded key, read as AppSettings read it), from the Rig just
// after the copy at launch (the values the settings store handed over), and from the Rig at the next launch (the same
// values read back from rig.json's JSON). The prior the shot joins must be the same, bit for bit, all three ways, and so
// must what the real code makes of it: the join into the job (joinLengthPrior), the recorded analysis context
// (captureAnalysisContext, which records capture.club.lengthPrior) and the job and its input stamp (buildAnalysisJob).
// capture_context.cpp's own lines around the header (the key, the face-on camera) are compiled by no test.
//
//   R1  one fixture entry: the same prior state all three ways, bit for bit (emaPx, varPx, n, disagreeRun)
//   R2  so the recorded context (capture.club.lengthPrior among it), the job's prior and the input stamp are the same
//       all three ways, through the real join, context builder and job builder
//   R3  the camera-move self-heal reads the same: another frame size joins no prior, all three ways; an entry with no
//       frame size is trusted all three ways
//   R4  a prior learned after a shot (learnClubLength, the members the writer sets) is the one the next freeze reads,
//       in the same run and after a relaunch: n + 1
//   R5  the members a shot learns are today's eight, by today's names
//
// IsolatedStores first (#187): the settings store and the Rig's folder are temporary, never the real ones.

#include "../../Core/tests/isolated_stores.h"

#include "../shot/club_length_prior_entry.h"
#include "../../Club/club_length_fusion.h"
#include "../../Core/pp_settings.h"
#include "../../DomainModel/rig.h"
#include "../../Stores/rig_store.h"
#include "../../Stores/recording_header.h"   // captureAnalysisContext, analysisContextJson: the recorded context
#include "../../Stores/swing_exporter.h"    // SwingExportJob: the job the context is built from
#include "../../Analysis/analysis_job_builder.h"   // buildAnalysisJob, the input stamp
#include "../../Analysis/effective_context.h"
#include "../../Vocabulary/Recording/recording.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QTimeZone>

#include <cstdio>
#include <cstring>

using namespace pinpoint;
using namespace pinpoint::rig;
using pinpoint::analysis::LengthPriorState;

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool sameState(const std::optional<LengthPriorState> &a, const std::optional<LengthPriorState> &b)
{
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return sameBits(a->emaPx, b->emaPx) && sameBits(a->varPx, b->varPx) && a->n == b->n && a->disagreeRun == b->disagreeRun;
}

// The shot's recorded context and job, through the real code, from the state it joined (or none): capture_context.cpp's
// join into the job (joinLengthPrior), the Recording's one context builder (captureAnalysisContext: the recorded
// capture.club.lengthPrior) and the one job builder (buildAnalysisJob: the job's prior and the input stamp).
struct Recorded {
    QByteArray               contextJson;   // the whole recorded context, compact
    recording::LengthPrior   prior;         // context.club.lengthPrior
    double                   jobPx = 0, jobVarPx = 0;
    int                      jobN  = 0;
    analysis::AnalysisInputs inputs;        // the input stamp
};
Recorded recordedFrom(const std::optional<LengthPriorState> &st)
{
    SwingExportJob ctxJob;
    ctxJob.clubName       = QStringLiteral("7 IRON");
    ctxJob.athleteHeightM = 1.80;
    if (st) capture::joinLengthPrior(*st, ctxJob);
    recording::Recording r;
    r.context = captureAnalysisContext(ctxJob, QString(), QString(), QDate(2026, 11, 27), QVariantMap{});
    const ShotAnalysisJob job =
        analysis::buildAnalysisJob(r, analysis::effectiveContext(r.context, analysis::ClubReview{}), analysis::RunOptions{});
    Recorded out;
    out.contextJson = QJsonDocument(analysisContextJson(r.context)).toJson(QJsonDocument::Compact);
    out.prior       = r.context.club.lengthPrior;
    out.jobPx       = job.priorClubLenPx;
    out.jobVarPx    = job.priorClubLenVarPx;
    out.jobN        = job.priorClubLenN;
    out.inputs      = job.inputs;
    return out;
}
bool sameRecorded(const Recorded &a, const Recorded &b)
{
    return a.contextJson == b.contextJson && sameBits(a.prior.px, b.prior.px) && sameBits(a.prior.varPx, b.prior.varPx)
           && a.prior.n == b.prior.n && sameBits(a.jobPx, b.jobPx) && sameBits(a.jobVarPx, b.jobVarPx) && a.jobN == b.jobN
           && a.inputs == b.inputs && a.inputs.stamped();
}

const QString kAthlete = QStringLiteral("5b0c7f1e-2a3d-4c5b-8e6f-7a8b9c0d1e2f");
const QString kCamera  = QStringLiteral("Blackfly S BFS-U3-16S2M|24157001");
const QString kOldCam  = QStringLiteral("FaceTime HD Camera|0x1420000005ac8600");
// SET-05's key, as capture_context.cpp built it on 8273d1cf, before C-1c.
QString todaysPriorKey(const QString &uuid, const QString &club, const QString &cameraKey)
{
    return uuid + QLatin1Char('|') + club + QLatin1Char('|') + cameraKey;
}

// The entry as the settings writer wrote it after a live shot (ShotProcessor before C-7): the update's values, which
// are not round numbers, so a conversion that lost a bit would show.
QVariantMap fixtureEntry()
{
    return QVariantMap{ { "emaPx", 512.3456789012345 }, { "varPx", 3.0 / 7.0 }, { "n", 6 }, { "disagreeRun", 1 },
                        { "lengthMm", 940 }, { "frameW", 1280 }, { "frameH", 1024 },
                        { "lastUpdatedUtc", QStringLiteral("2026-11-27T08:00:00Z") } };
}

// Today's read before C-7: AppSettings loaded the whole map (ppSettings().value(key, QVariantMap{}).toMap()), and
// capture_context.cpp looked the shot's entry up by the key's spelling.
QVariantMap settingsEntry(const QString &key)
{
    return ppSettings().value(QStringLiteral("analysis/clubLenPrior"), QVariantMap{}).toMap().value(key).toMap();
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("club_length_prior_test — the temporary directory: %s\n", qPrintable(stores.path()));
    check(stores.contains(ppSettings().fileName()), "the settings store is the temporary one");

    const QString keyA   = todaysPriorKey(kAthlete, QStringLiteral("7i"), kCamera);
    const QString keyOld = todaysPriorKey(kAthlete, QStringLiteral("PW"), kOldCam);   // an entry from before frame sizes
    QVariantMap noDims = fixtureEntry();
    noDims.remove("frameW");
    noDims.remove("frameH");
    {
        QSettings s = ppSettings();
        s.setValue(QStringLiteral("analysis/clubLenPrior"), QVariantMap{ { keyA, fixtureEntry() }, { keyOld, noDims } });
        s.sync();
    }
    const ClubLengthPriorKey typedA(AthleteId::fromString(kAthlete), QStringLiteral("7i"), DeviceKey::fromPersistedCameraKey(kCamera));
    const ClubLengthPriorKey typedOld(AthleteId::fromString(kAthlete), QStringLiteral("PW"), DeviceKey::fromPersistedCameraKey(kOldCam));
    const QString folder = stores.path() + "/rig";
    const QList<RigStore::Group> groups{ RigStore::Group::Cameras, RigStore::Group::ClubLengthPrior };

    const QVariantMap before = settingsEntry(keyA), beforeOld = settingsEntry(keyOld);
    QVariantMap afterCopy, afterCopyOld, afterReload, afterReloadOld;
    {
        RigStore store(folder, groups);
        Rig      rig(store);
        rig.load();   // the copy at launch
        afterCopy    = clubLengthPriorOf(rig.parts().other, typedA);
        afterCopyOld = clubLengthPriorOf(rig.parts().other, typedOld);
    }
    {
        RigStore store(folder, groups);
        Rig      rig(store);
        rig.load();   // the next launch: read back from rig.json, nothing copied
        afterReload    = clubLengthPriorOf(rig.parts().other, typedA);
        afterReloadOld = clubLengthPriorOf(rig.parts().other, typedOld);
    }
    check(!before.isEmpty() && !afterCopy.isEmpty() && !afterReload.isEmpty(), "the fixture's entry is found all three ways");

    std::printf("\nR1 the prior state the shot joins\n");
    const auto s0 = capture::lengthPriorFromEntry(before, 1280, 1024);
    const auto s1 = capture::lengthPriorFromEntry(afterCopy, 1280, 1024);
    const auto s2 = capture::lengthPriorFromEntry(afterReload, 1280, 1024);
    check(s0 && s0->n == 6 && s0->disagreeRun == 1 && sameBits(s0->emaPx, 512.3456789012345) && sameBits(s0->varPx, 3.0 / 7.0),
          "R1a before: the fixture's state, as the settings store held it");
    check(sameState(s0, s1), "R1b after the copy: the same state, bit for bit");
    check(sameState(s0, s2), "R1c after a relaunch (rig.json read back): the same state, bit for bit");

    std::printf("\nR2 the recorded capture.club.lengthPrior\n");
    const Recorded r0 = recordedFrom(s0);
    check(r0.prior.present() && r0.prior.n == 6 && sameBits(r0.prior.px, 512.3456789012345) && r0.jobN == 6
              && r0.contextJson.contains("lengthPrior") && r0.inputs.stamped(),
          "R2a before: capture.club.lengthPrior recorded (n 6), the job carries it, the input stamp made");
    check(sameRecorded(r0, recordedFrom(s1)) && sameRecorded(r0, recordedFrom(s2)),
          "R2b the same recorded context, job prior and input stamp all three ways");
    check(!recordedFrom(std::nullopt).prior.present() && recordedFrom(std::nullopt).inputs == r0.inputs,
          "R2c no prior joined: none recorded, and the input stamp is the same (it holds no member of the prior)");

    std::printf("\nR3 the camera-move self-heal\n");
    check(!capture::lengthPriorFromEntry(before, 1920, 1080) && !capture::lengthPriorFromEntry(afterCopy, 1920, 1080)
              && !capture::lengthPriorFromEntry(afterReload, 1920, 1080),
          "R3a another frame size: no prior joined, all three ways");
    const auto o0 = capture::lengthPriorFromEntry(beforeOld, 1920, 1080);
    check(o0 && sameState(o0, capture::lengthPriorFromEntry(afterCopyOld, 1920, 1080))
              && sameState(o0, capture::lengthPriorFromEntry(afterReloadOld, 1920, 1080)),
          "R3b an entry with no frame size is trusted, the same all three ways");

    std::printf("\nR4 a prior learned after a shot is the next freeze's\n");
    {
        RigStore store(folder, groups);
        Rig      rig(store);
        rig.load();
        LengthPriorState st = *capture::lengthPriorFromEntry(clubLengthPriorOf(rig.parts().other, typedA), 1280, 1024);
        analysis::updateLengthPrior(st, st.emaPx + 0.2, 0.9, analysis::LengthFusionConfig{});   // the analysis's fold, inside the gate
        rig.learnClubLength(typedA, capture::learnedLengthPrior(st, 0.94, 1280, 1024, QDateTime::currentDateTimeUtc()));
        const auto next = capture::lengthPriorFromEntry(clubLengthPriorOf(rig.parts().other, typedA), 1280, 1024);
        check(next && next->n == 7 && sameState(next, st), "R4a the same run: the next freeze joins n 7, as learned");
        RigStore store2(folder, groups);
        Rig      again(store2);
        again.load();
        const auto relaunched = capture::lengthPriorFromEntry(clubLengthPriorOf(again.parts().other, typedA), 1280, 1024);
        check(relaunched && relaunched->n == 7 && sameState(relaunched, st),
              "R4b after a relaunch: n 7 from rig.json, not the settings' n 6 copied again");
        check(settingsEntry(keyA).value("n").toInt() == 6, "R4c the settings key is not written");
    }

    std::printf("\nR5 the members a shot learns\n");
    {
        LengthPriorState st;
        st.emaPx = 500.5; st.varPx = 2.25; st.n = 3; st.disagreeRun = 2;
        const QDateTime now = QDateTime(QDate(2026, 11, 27), QTime(9, 30, 0), QTimeZone::UTC);
        const QVariantMap m = capture::learnedLengthPrior(st, 0.9406, 1440, 1080, now);
        QStringList keys = m.keys();
        keys.sort();
        check(keys == QStringList{ "disagreeRun", "emaPx", "frameH", "frameW", "lastUpdatedUtc", "lengthMm", "n", "varPx" },
              "R5a today's eight members, by today's names");
        check(m.value("emaPx").toDouble() == 500.5 && m.value("varPx").toDouble() == 2.25 && m.value("n").toInt() == 3
                  && m.value("disagreeRun").toInt() == 2 && m.value("lengthMm").toInt() == 941 && m.value("frameW").toInt() == 1440
                  && m.value("frameH").toInt() == 1080 && m.value("lastUpdatedUtc").toString() == "2026-11-27T09:30:00Z",
              "R5b each as today's writer set it (the length in whole millimetres, rounded; the time in ISO form, UTC)");
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
