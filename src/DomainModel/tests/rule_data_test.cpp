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

// rule_data_test — RuleData, the one owner of the packs and norms (refactor step 3.2; Q-02 closed by D5:
// DEC-170; information_model.md OBJ-PACKS-NORMS, CLS-PACKS-NORMS, EVT-12, EVT-13, R-CON-4, FILE-APP-01…05,
// SET-06, SET-15). Over a store in a temporary directory, with the settings file redirected there too:
//
//   1  one snapshot, shared: every interim accessor answers from the same assembled objects
//   2  an edit publishes ONE new snapshot and its event; the old snapshot lives on, unchanged, in a holder
//   3  a norm edit reaches the process-wide norm slot the analysis passes read
//   4  a user file written by a newer build is read-only: the save is refused, nothing is published
//   5  an old user.norms.json without the standing keys loads unchanged and is written as it was;
//      a row with a standing round-trips and declares schema 3 (IM-S3-6: DEC-186)
//   6  SET-06 and SET-15 are read and written where they are stored, through the repository
//   7  with no RuleData installed, the accessors answer from an empty snapshot
//
//   cmake --build build/tests --target rule_data_test -j 6
//   ctest --test-dir build/tests -R '^rule_data_test$' --output-on-failure

#include "../rule_data.h"
#include "../../Export/rule_data_store.h"
#include "../../Diagnostics/derived/strike_from_flight.h"   // FlightNorms: the store's read of the flight table (7.1)
#include "../../Core/pp_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using namespace pinpoint;
using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

int main(int argc, char **argv)
{
    // The settings file (SET-06, SET-15) goes in this run's own directory, never the user's.
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    QTemporaryDir    tmp;
    if (!tmp.isValid()) { std::printf("no temporary directory\nFAILED\n"); return 1; }
    const QString dataDir = tmp.filePath(QStringLiteral("diagnostics"));
    QDir().mkpath(dataDir);

    std::printf("7  no RuleData installed: an empty snapshot\n");
    {
        check(RuleData::installed() == nullptr && RuleData::current()->serial == 0,
              "current() answers the empty snapshot");
        check(makeCharacteristicPackProvider()->pack().conditions.empty() && sharedDrillSet().drills.empty(),
              "…and every accessor reads nothing from it, rather than a null");
        QString why;
        check(!saveUserPack(CharacteristicPack{}, &why) && !why.isEmpty(), "a save with no owner is refused, in words");
    }

    RuleData rules(std::make_unique<RuleDataStore>(dataDir));
    RuleData::install(&rules);

    std::printf("1  one snapshot, shared\n");
    const std::shared_ptr<const RuleDataSnapshot> first = rules.snapshot();
    {
        check(first && first->serial == 1 && first == RuleData::current(), "the first snapshot is published at construction");
        check(!first->pack->pack().conditions.empty() && !first->norms->norms().norms.empty(),
              "it holds the shipped pack and norms (through the PINPOINT_CORE_* seams)");
        const auto a = makeCharacteristicPackProvider();
        const auto b = makeCharacteristicPackProvider();
        check(&a->pack() == &b->pack() && &a->pack() == &first->pack->pack(),
              "two readers' providers are views of ONE assembled pack (no private parse)");
        check(&sharedDrillSet() == &first->drills->merged && &sharedReferenceSet() == &first->references->merged
                  && &sharedScreenSet() == &first->screens->merged && &sharedGateWordSet() == &first->gateWords->merged,
              "the content sets are the snapshot's");
        check(sharedNormProvider() == first->norms, "the process-wide norm slot hands out the snapshot's norms");
        check(first->gradePolicy == QStringLiteral("standard") && first->disabledNormSets.isEmpty(),
              "SET-15 and SET-06 read as their defaults when unset");
    }

    std::printf("2  an edit publishes one new snapshot; the old one lives on\n");
    const Condition *shippedCondition = first->pack->pack().conditions.empty() ? nullptr : &first->pack->pack().conditions.front();
    const QString    oldGoal          = shippedCondition ? shippedCondition->goal : QString();
    {
        QSignalSpy packSpy(&rules, &RuleData::packChanged);
        QSignalSpy normSpy(&rules, &RuleData::normsChanged);
        CharacteristicPack user;
        user.id      = QStringLiteral("user");
        user.version = QStringLiteral("1");
        if (shippedCondition) {
            Condition c = *shippedCondition;
            c.goal      = QStringLiteral("a goal written by rule_data_test");
            user.conditions.push_back(c);
        }
        QString why;
        check(rules.saveUserPack(user, &why), "the user pack is written through RuleData's command");
        const std::shared_ptr<const RuleDataSnapshot> next = rules.snapshot();
        check(next != first && next->serial == first->serial + 1, "ONE new snapshot was published");
        check(packSpy.count() == 1 && normSpy.count() == 0
                  && packSpy.at(0).at(0).toString() == userPackFileName(),
              "…with ONE EVT-13 PackChanged naming the user pack, and no EVT-12");
        const Condition *now = shippedCondition ? next->pack->pack().condition(shippedCondition->id) : nullptr;
        check(now && now->goal == QStringLiteral("a goal written by rule_data_test"), "the new snapshot reads the edit");
        check(shippedCondition && first->pack->pack().condition(shippedCondition->id)->goal == oldGoal,
              "the snapshot a holder kept still reads what it read (R-CON-4)");
        check(next->norms == first->norms && next->drills == first->drills,
              "the parts the edit did not touch are shared, not re-read");
        check(QFile::exists(QDir(dataDir).filePath(QStringLiteral("user.json"))), "the file is where the store keeps it");
    }

    std::printf("3  a norm edit reaches the norm slot\n");
    {
        QSignalSpy     normSpy(&rules, &RuleData::normsChanged);
        const quint64  g0   = sharedNormProviderGeneration();
        const auto     held = sharedNormProvider();
        const Norm    &row  = held->norms().norms.front();
        NormPack       user;
        user.id      = QStringLiteral("user");
        user.version = QStringLiteral("1");
        Norm edited  = row;
        edited.mu += 1.0;
        user.norms.push_back(edited);
        QString why;
        check(rules.saveUserNorms(user, &why), "the user norm set is written through RuleData's command");
        check(normSpy.count() == 1, "…with ONE EVT-12 NormsChanged");
        check(sharedNormProviderGeneration() > g0 && sharedNormProvider() == rules.snapshot()->norms
                  && sharedNormProvider() != held,
              "the slot moved on to the new snapshot's norms");
        const NormResolution r = sharedNormProvider()->resolve(row.measureId, row.contextId, row.cohort);
        check(r.found() && r.norm->mu == edited.mu, "…which grade with the edit");
        const NormResolution h = held->resolve(row.measureId, row.contextId, row.cohort);
        check(h.found() && h.norm->mu == row.mu, "the norms a reader held still answer what they answered");
    }

    std::printf("4  a user file from a newer build is read-only\n");
    {
        const QString userPack = QDir(dataDir).filePath(QStringLiteral("user.json"));
        const QByteArray newer = QByteArrayLiteral("{\"id\":\"user\",\"version\":\"9\",\"schemaVersion\":99,\"conditions\":[]}");
        check(writeAll(userPack, newer), "a user.json declaring schema 99 is on disk");
        rules.reload(RuleData::Packs);
        const std::shared_ptr<const RuleDataSnapshot> before = rules.snapshot();
        check(before->newerUserFiles.contains(userPack), "the read marks it newer than this build (B-20)");
        QSignalSpy packSpy(&rules, &RuleData::packChanged);
        QString    why;
        check(!rules.saveUserPack(CharacteristicPack{}, &why) && why.contains(QLatin1String("newer")),
              "a save over it is refused, saying why");
        check(readAll(userPack) == newer, "…and the file is untouched");
        check(rules.snapshot() == before && packSpy.count() == 0, "a refused write publishes nothing");
        QFile::remove(userPack);
        rules.reload(RuleData::Packs);
    }

    std::printf("5  the standing keys (IM-S3-6), and an old user.norms.json\n");
    {
        const QString userNorms = QDir(dataDir).filePath(QStringLiteral("user.norms.json"));
        const QByteArray old = QByteArrayLiteral(
            "{\"id\":\"user\",\"version\":\"1\",\"schemaVersion\":1,\"norms\":[{\"measure\":\"m_x\",\"context\":\"any\","
            "\"mu\":1.5,\"sigmaLo\":0.5,\"unit\":\"cm\",\"source\":\"heuristic\",\"citation\":\"an old note\"}]}");
        check(writeAll(userNorms, old), "a schema-1 user norm set without standing keys is on disk");
        const NormPackLoadResult res = loadNormPack(old, userNorms);
        check(res.parsed && res.pack.norms.size() == 1 && res.pack.norms.front().standing == NormStanding::Unstated
                  && res.pack.norms.front().decidedBy.isEmpty() && !res.pack.norms.front().decidedOn.isValid(),
              "it loads, its row unstated");
        const QJsonObject back = saveNormPack(res.pack);
        const QJsonObject row  = back.value(QStringLiteral("norms")).toArray().at(0).toObject();
        check(back.value(QStringLiteral("schemaVersion")).toInt() == 1 && !row.contains(QStringLiteral("standing"))
                  && !row.contains(QStringLiteral("decidedBy")) && !row.contains(QStringLiteral("decidedOn")),
              "…and is written back as it was: schema 1, no standing keys (upgraded only on an edit that adds one)");
        rules.reload(RuleData::Norms);
        check(rules.snapshot()->newerUserFiles.isEmpty(), "an older file is not read-only");

        NormPack withStanding = res.pack;
        withStanding.norms.front().standing  = NormStanding::Provisional;
        withStanding.norms.front().decidedBy = QStringLiteral("Mark");
        withStanding.norms.front().decidedOn = QDate(2026, 11, 13);
        const QJsonObject s3 = saveNormPack(withStanding);
        const NormPackLoadResult again = loadNormPack(QJsonDocument(s3).toJson(), QStringLiteral("again"));
        check(s3.value(QStringLiteral("schemaVersion")).toInt() == 3 && again.parsed
                  && again.pack.norms.front().standing == NormStanding::Provisional
                  && again.pack.norms.front().decidedBy == QStringLiteral("Mark")
                  && again.pack.norms.front().decidedOn == QDate(2026, 11, 13),
              "a row with a standing declares schema 3 and round-trips its three keys");
        QFile::remove(userNorms);
    }

    std::printf("6  SET-06 and SET-15 where they are stored\n");
    {
        QSignalSpy normSpy(&rules, &RuleData::normsChanged);
        rules.setGradePolicy(QStringLiteral("strict"));
        rules.setDisabledNormSets({ QStringLiteral("community.one") });
        check(ppSettings().value(QStringLiteral("ui/diagnosticsGradePolicy")).toString() == QStringLiteral("strict")
                  && ppSettings().value(QStringLiteral("ui/diagnosticsNormSetsOff")).toStringList()
                         == QStringList{ QStringLiteral("community.one") },
              "both are written under today's settings keys");
        check(rules.snapshot()->gradePolicy == QStringLiteral("strict")
                  && rules.snapshot()->disabledNormSets == QStringList{ QStringLiteral("community.one") },
              "the snapshot records the values in force");
        check(normSpy.count() == 2, "each change is one EVT-12");
        rules.setGradePolicy(QStringLiteral("strict"));
        check(normSpy.count() == 2, "setting the value already in force publishes nothing");
    }

    // THE FLIGHT TABLE IS THE STORE'S TO READ (refactor step 7.1: the rules' own loader is gone): the file
    // PINPOINT_CORE_FLIGHT_NORMS names, else the resource; a table that can't be read is empty, so nothing
    // is read from the flight.
    {
        const QByteArray was = qgetenv("PINPOINT_CORE_FLIGHT_NORMS");
        const analysis::FlightNorms shipped = RuleDataStore().loadFlightNorms();
        check(!was.isEmpty() && shipped.schema == 2 && !shipped.isEmpty(), "the store reads the shipped flight table, schema 2");
        qputenv("PINPOINT_CORE_FLIGHT_NORMS", QByteArray("/nonexistent/flight_norms.json"));
        check(RuleDataStore().loadFlightNorms().isEmpty(), "a flight table that can't be read is empty");
        qputenv("PINPOINT_CORE_FLIGHT_NORMS", was);
    }

    RuleData::install(nullptr);
    check(RuleData::current()->serial == 0, "uninstalled, current() is the empty snapshot again");

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
