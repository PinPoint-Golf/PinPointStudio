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

// fire_findings.h on one swing (refactor step 3.3; R-STAMP-1). The fixture is lm_7iron
// (src/Diagnostics/tests/data/live_swings: a 7 iron with launch-monitor readings), staged into a temporary
// library and read as the session model reads it (readLivePhaseGrid). Against the shipped content (the
// PINPOINT_CORE_* seams), it shows:
//   • the functions are pure: the same readings and inputs give the same record, and the same stamp;
//   • EVERY FIELD OF FiringInputs MATTERS: changing it changes the stamp (firingStamp) — and the output where
//     this swing can show it: the pack, the norms, the cohort and the club baseline change the findings, the
//     flight table the derived data; the grade policy, σ and the usuals are printed for what they do here;
//     SET-06 is recorded and changes nothing else (it is not applied, as before);
//   • a shot whose recording is broken says nothing: every row and check not assessable, no peak time;
//   • deriveShotData() is composeShotOutcome() and shotFactors() on the inputs' usuals and baseline.

#include "../engine/fire_findings.h"
#include "../../DomainModel/rule_data.h"
#include "../../Export/rule_data_install.h"
#include "../../Coaching/shot_outcomes_pack.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// <tmp>/live_swings/<name>/swing.json — the library's shape: the grid names its session after the parent.
static QString stageFixture(const QTemporaryDir &tmp, const char *name)
{
    const QString dst = QDir(tmp.path()).filePath(QLatin1String("live_swings/") + QLatin1String(name));
    if (!QDir().mkpath(dst)) return QString();
    const QString src = QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
    return QFile::copy(src, QDir(dst).filePath(QStringLiteral("swing.json"))) ? dst : QString();
}

static QByteArray recordJson(const FiredShot &f)
{
    return QJsonDocument(toJson(std::vector<ShotRecord>{ f.record })).toJson(QJsonDocument::Compact)
         + (f.hasLaunchMonitor ? "lm" : "nolm");
}

static QByteArray factorsJson(const ShotFactors &f)
{
    QByteArray out;
    for (const ShotFactor *p : { &f.strike, &f.direction, &f.flight })
        out += (p->read ? "read:" : "unread:") + p->values.join(QLatin1Char(',')).toUtf8() + '|'
             + p->faults.join(QLatin1Char(',')).toUtf8() + '|' + p->method.toUtf8() + ';';
    return out;
}

static QByteArray outcomeJson(const ShotOutcome &o)
{
    QByteArray out = (o.ready ? "ready;" : "notready;") + o.phrase.toUtf8() + ';';
    for (const ShotOutcomeLabel &l : o.labels) out += l.id.toUtf8() + ',';
    out += ';' + QByteArray::number(o.carry.shortBy, 'g', 17);
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // No user layer on this machine can reach the content: the shipped pack (its own layer of the snapshot), the
    // shipped norms (the seams) and the shipped flight table.
    QStandardPaths::setTestModeEnabled(true);
    pinpoint::installProcessRuleData();
    std::printf("fire_findings_test\n");

    const std::shared_ptr<const pinpoint::RuleDataSnapshot> snap = pinpoint::RuleData::current();
    const std::shared_ptr<const ICharacteristicPackProvider> pack = snap->shippedPack;
    const std::shared_ptr<const INormProvider> norms{ makeResourceNormProvider().release() };
    check(pack && !pack->pack().measures.empty() && norms && !norms->norms().norms.empty() && snap->flightNorms
              && !snap->flightNorms->isEmpty(),
          "the shipped pack, norms and flight table are read");

    QTemporaryDir tmp;
    const QString swing = stageFixture(tmp, "lm_7iron");
    check(!swing.isEmpty(), "the fixture is staged");
    const SwingPhaseGrid grid = readLivePhaseGrid(swing, pack->pack());
    check(!grid.isEmpty() && grid.club == QLatin1String("7 IRON"), "its grid is read: a declared 7 iron");
    const QString club = pinpoint::canonicalClubId(grid.club);

    const auto readingsOf = [&grid](bool broken = false) {
        FiringReadings r;
        r.grid        = grid;
        r.handedness  = QStringLiteral("right");
        r.dataWarning = broken;
        r.captureBroken = broken;
        return r;
    };

    // ── The base inputs: a frozen baseline the history measures can read against, and a usual ──
    FiringInputs base;
    base.pack        = pack;
    base.norms       = norms;
    base.gradePolicy = QStringLiteral("standard");
    base.flightNorms = *snap->flightNorms;
    ClubBaselineEntry entry;
    entry.club          = club;
    entry.carryMean     = 200.0;   // far past this shot's carry: the carry shortfall reads
    entry.carrySd       = 8.0;
    entry.ballSpeedMean = 140.0;
    entry.ballSpeedSd   = 3.0;
    entry.speedMean     = 84.0;    // this shot's 82 mph is a full swing against it (no backswing usual: the speed answers)
    entry.smashMean     = 1.40;
    entry.n             = 30;
    base.clubBaseline   = QHash<QString, ClubBaselineEntry>{ { club, entry } };
    base.usuals         = QHash<QString, ClubFlightUsual>{ { club, ClubFlightUsual{ 18.0, 1.38, -4.0, 170.0 } } };

    std::printf("\npure\n");
    const FiredShot a = fireFindings(readingsOf(), base);
    const FiredShot b = fireFindings(readingsOf(), base);
    check(a.ok && !a.record.rows.empty() && !a.record.signalRows.empty() && a.hasLaunchMonitor,
          "fires: rows, sequence checks and the monitor's readings");
    check(recordJson(a) == recordJson(b), "the same readings and inputs give the same record");
    const QString stamp = firingStamp(base);
    check(stamp.size() == 16 && stamp == firingStamp(base), "the stamp is 16 hex and the same for the same inputs");
    check(a.record.club == QLatin1String("7 IRON") && a.record.swingLength == QLatin1String("full")
              && a.record.handedness == QLatin1String("Right"),
          "the record carries the club, the swing length (layer 3) and the hand, normalised");
    check(!fireFindings(readingsOf(), FiringInputs{}).ok, "no pack or norms: nothing fires");

    const ShotOutcomePack outcomePack = shotOutcomePackFor(&pack->pack());
    const ShotOutcomeSessionFacts diagnosis;
    const ShotDerivedData derived = deriveShotData(a.record, {}, QString(), outcomePack, diagnosis, base);

    // ── Every input matters ─────────────────────────────────────────────────────────────────────────────
    std::printf("\nevery input of the stamp matters\n");
    const QByteArray baseJson = recordJson(a);
    const auto findingsMove = [&](const FiringInputs &in) { return recordJson(fireFindings(readingsOf(), in)) != baseJson; };
    const auto derivedMove = [&](const FiringInputs &in) {
        const ShotDerivedData d = deriveShotData(a.record, {}, QString(), outcomePack, diagnosis, in);
        return factorsJson(d.factors) != factorsJson(derived.factors) || outcomeJson(d.outcome) != outcomeJson(derived.outcome);
    };

    {   // the pack: the monitor's launch angle held — named as ungraded, and the stamp moves
        CharacteristicPack held = pack->pack();
        bool found = false;
        for (Measure &m : held.measures)
            if (m.id == QLatin1String("m_lmLaunchAngle")) { m.status = MeasureStatus::Held; found = true; }
        FiringInputs in = base;
        in.pack = std::shared_ptr<const ICharacteristicPackProvider>(makeMemoryPackProvider(held, QStringLiteral("held"), PackOrigin::Core).release());
        const FiredShot f = fireFindings(readingsOf(), in);
        check(found && f.record.ungradedReadings.contains(QStringLiteral("m_lmLaunchAngle")) && findingsMove(in),
              "pack: a measure held changes the findings");
        check(firingStamp(in) != stamp, "pack: …and the stamp");
    }

    // The first row graded against a corridor: the norms and the cohort move it.
    const ConditionRow *graded = nullptr;
    for (const ConditionRow &r : a.record.rows)
        if (r.corridorShape == CorridorShape::TwoSided && std::isfinite(r.value) && !r.drivingMeasureId.isEmpty()) { graded = &r; break; }
    check(graded != nullptr, "a row graded against a two-sided corridor, to move");
    {   // the norms: that measure's corridors shifted by ten of their own widths
        NormPack shifted = norms->norms();
        for (Norm &n : shifted.norms)
            if (graded && n.measureId == graded->drivingMeasureId) n.mu += 10.0 * std::max(n.sigmaHi, 1e-3);
        FiringInputs in = base;
        in.norms = std::shared_ptr<const INormProvider>(makeMemoryNormProvider(shifted, norms->contexts(), QStringLiteral("shifted"), PackOrigin::Core).release());
        check(findingsMove(in), "norms: a corridor moved changes the findings");
        check(firingStamp(in) != stamp, "norms: …and the stamp");
    }
    {   // the cohort: a women's row for that measure, read only for a woman
        NormPack withCohort = norms->norms();
        std::vector<Norm> extra;
        for (const Norm &n : withCohort.norms)
            if (graded && n.measureId == graded->drivingMeasureId && n.cohort.isUnqualified()) {
                Norm w = n;
                w.cohort.sex = Sex::Female;
                w.mu += 10.0 * std::max(n.sigmaHi, 1e-3);
                extra.push_back(w);
            }
        for (const Norm &w : extra) withCohort.norms.push_back(w);
        FiringInputs men = base;
        men.norms = std::shared_ptr<const INormProvider>(makeMemoryNormProvider(withCohort, norms->contexts(), QStringLiteral("cohort"), PackOrigin::Core).release());
        FiringInputs women = men;
        women.cohort.sex = Sex::Female;
        check(!extra.empty() && !findingsMove(men) && findingsMove(women),
              "cohort: a cohort's own row is read for that cohort only");
        FiringInputs aged = base;
        aged.cohort.age = AgeBand::Adult55_64;
        check(firingStamp(women) != firingStamp(men) && firingStamp(aged) != stamp, "cohort: …and the stamp records it");
    }
    {   // the grade policy (SET-15)
        FiringInputs in = base;
        in.gradePolicy = QStringLiteral("strict");
        std::printf("        (the strict policy %s the findings on this swing)\n", findingsMove(in) ? "changes" : "does not change");
        check(firingStamp(in) != stamp, "grade policy: the stamp records it");
    }
    {   // the disabled norm sets (SET-06): recorded, not applied
        FiringInputs in = base;
        in.disabledNormSets = { QStringLiteral("core") };
        check(!findingsMove(in) && !derivedMove(in), "disabled norm sets: recorded, never applied — the output is the same");
        check(firingStamp(in) != stamp, "disabled norm sets: …and the stamp records them");
    }
    {   // the club baseline: none, and the history measures cannot read
        FiringInputs none = base;
        none.clubBaseline.reset();
        check(findingsMove(none), "club baseline: without one, the findings change (the history measures)");
        FiringInputs other = base;
        (*other.clubBaseline)[club].carryMean = 150.0;
        check(firingStamp(none) != stamp && firingStamp(other) != stamp && firingStamp(none) != firingStamp(other),
              "club baseline: …and the stamp records whether there is one and what it holds");
    }
    {   // σ on the readings
        FiringInputs in = base;
        in.withSigma = !base.withSigma;
        // This fixture's readings carry no σ of their own (a monitor shot with no camera track), so the switch
        // moves only the stamp here; session_diagnostics_model_test and live_measure_source_test pin what it does.
        std::printf("        (without σ the findings on this swing %s)\n", findingsMove(in) ? "change" : "do not change");
        check(firingStamp(in) != stamp, "σ: …and the stamp");
    }
    {   // the flight table: every landing band moved out of reach, so the flight reads differently
        FiringInputs in = base;
        for (auto it = in.flightNorms.landing.begin(); it != in.flightNorms.landing.end(); ++it) *it = FlightBand{ 89.0, 90.0 };
        for (auto it = in.flightNorms.apexCarry.begin(); it != in.flightNorms.apexCarry.end(); ++it) *it = FlightBand{ 5.0, 6.0 };
        check(derived.factors.flight.read && derivedMove(in), "flight norms: the derived data change");
        check(!findingsMove(in), "flight norms: …and the findings do not (they are layer 3's)");
        check(firingStamp(in) != stamp, "flight norms: …and the stamp");
    }
    {   // the usuals: none for the club, and the session's flight usual moved
        FiringInputs none = base;
        none.usuals.clear();
        FiringInputs moved = base;
        moved.usuals[club].launch = 40.0;
        moved.usuals[club].smash  = 1.6;
        std::printf("        (no usual %s the derived data; a moved one %s them)\n", derivedMove(none) ? "changes" : "does not change",
                    derivedMove(moved) ? "changes" : "does not change");
        check(!findingsMove(none), "usuals: the findings do not read them (layer 3's)");
        check(firingStamp(none) != stamp && firingStamp(moved) != stamp && firingStamp(none) != firingStamp(moved),
              "usuals: the stamp records them");
    }
    {   // the stamp's own structure: the content stamp's inputs and the rule versions all feed it
        FiringInputs noPack = base;
        noPack.pack.reset();
        FiringInputs noNorms = base;
        noNorms.norms.reset();
        check(firingStamp(noPack) != stamp && firingStamp(noNorms) != stamp, "the stamp hashes the pack and the norms");
    }

    // ── A broken recording says nothing ───────────────────────────────────────────────────────────────
    std::printf("\nintegrity\n");
    {
        const FiredShot f = fireFindings(readingsOf(/*broken=*/true), base);
        bool allWithheld = !f.record.rows.empty();
        for (const ConditionRow &r : f.record.rows)
            allWithheld = allWithheld && r.state == ShotState::NotAssessable
                       && r.notAssessableReason == missingReasonText(MissingKind::CaptureDataIssue);
        for (const SignalRow &g : f.record.signalRows)
            allWithheld = allWithheld && g.state == ShotState::NotAssessable;
        check(f.record.dataWarning && allWithheld && f.record.peakTimes.empty(),
              "frames lost: every row and check not assessable, no peak time, the warning kept");
        FiringReadings warnOnly = readingsOf();
        warnOnly.dataWarning = true;
        const FiredShot w = fireFindings(warnOnly, base);
        check(w.record.dataWarning && recordJson(w) != recordJson(f), "a warning without a broken recording withholds nothing");
    }

    // ── Layer 3 is the Coaching rules, composed ───────────────────────────────────────────────────────
    std::printf("\nderived data\n");
    {
        const ShotOutcomeSessionFacts facts{ {}, {}, base.usuals };
        const ShotOutcome o = composeShotOutcome(a.record, {}, QString(), &entry, outcomePack, facts);
        const ShotFactors f = shotFactors(a.record, o, outcomePack, base.flightNorms, base.usuals);
        check(outcomeJson(derived.outcome) == outcomeJson(o) && factorsJson(derived.factors) == factorsJson(f),
              "deriveShotData = composeShotOutcome + shotFactors, on the inputs' usual and usuals");
        check(factorsJson(deriveShotFactors(a.record, o, outcomePack, base)) == factorsJson(f),
              "deriveShotFactors = shotFactors on the inputs' flight table and usuals");
        check(clubUsualOf(base, QStringLiteral("7 iron")) != nullptr && clubUsualOf(base, QStringLiteral("DRIVER")) == nullptr
                  && clubUsualOf(FiringInputs{}, club) == nullptr,
              "clubUsualOf: any spelling of the club; none without a baseline");
    }

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "passed", g_fail);
    return g_fail ? 1 : 0;
}
