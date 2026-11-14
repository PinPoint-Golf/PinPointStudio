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

// The process-wide norm set (sharedNormProvider(), merged_norm_provider.cpp) as the rest of the app
// reads it: an immutable snapshot that a saved corridor replaces, safely, while other threads read.
//
//   * SAVE → RESET → THE EDIT. A user norm set written to disk and followed by
//     resetSharedNormProvider() is what the next take returns: the edited band, without a relaunch
//     (audit B-01). Without the reset the take still returns the old set — the reason the save has to
//     call it.
//   * A SNAPSHOT IS A SNAPSHOT. A provider taken before the reset still answers the old band after it:
//     a reader mid-pass finishes on the norms it started with, and nothing it holds is freed.
//   * THE GENERATION MOVES with every reset, and sharedNormProvider(&gen) reports the generation of the
//     snapshot it returned, so a holder that derives something from the norms can tell it is stale.
//   * THREADS (audit B-03). Many first callers at once get ONE assembly; resets interleaved with
//     readers on other threads never hand out an empty, half-built or freed provider. (A data race the
//     run happens not to trip would only show under TSan; this pins the behaviour.)
//
//   cmake --build build/tests --target shared_norm_provider_test -j 6
//   ctest --test-dir build/tests -R '^shared_norm_provider_test$' --output-on-failure

#include "../norms/norm_provider.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static const QString kMeasure = QStringLiteral("m_ballPosition");
static const QString kContext = QStringLiteral("driver");

static double muOf(const std::shared_ptr<const INormProvider> &p)
{
    if (!p) return std::nan("");
    const NormResolution r = p->resolve(kMeasure, kContext);
    return r.found() ? r.norm->mu : std::nan("");
}

// The user's own norm set holding one row: the shipped row at (kMeasure, kContext) with `mu`.
static bool writeUserRow(const Norm &shipped, double mu)
{
    NormPack pack;
    pack.id      = QStringLiteral("user");
    pack.version = QStringLiteral("1");
    Norm n       = shipped;
    n.mu         = mu;
    pack.norms.push_back(n);
    QString why;
    const bool ok = saveUserNormPack(pack, &why);
    if (!ok) std::printf("      could not write the user norm set: %s\n", qPrintable(why));
    return ok;
}

// N threads that wait at one gate, then each run `body` once.
template <typename F>
static void runTogether(int n, F body)
{
    std::atomic<int>       waiting{ n };
    std::vector<QThread *> threads;
    for (int i = 0; i < n; ++i) {
        threads.push_back(QThread::create([&waiting, body, i]() {
            waiting.fetch_sub(1);
            while (waiting.load() > 0) QThread::yieldCurrentThread();
            body(i);
        }));
        threads.back()->start();
    }
    for (QThread *t : threads) {
        t->wait();
        delete t;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // saveUserNormPack() writes under AppDataLocation: test mode keeps that away from a real profile.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(userNormPath());
    resetSharedNormProvider();

    std::printf("=== save -> reset -> the next take reads the edited band ===\n");
    double shippedMu = std::nan("");
    double editedMu  = std::nan("");
    {
        quint64                                    g0 = 0;
        const std::shared_ptr<const INormProvider> before = sharedNormProvider(&g0);
        check(before != nullptr, "the shared norm set assembles");
        const NormResolution shipped = before ? before->resolve(kMeasure, kContext) : NormResolution{};
        check(shipped.found() && !shipped.overridden, "ball position at driver resolves to a shipped row");
        if (!shipped.found()) {
            std::printf("FAILURES\n");
            return 1;
        }
        shippedMu = shipped.norm->mu;
        editedMu  = shippedMu + 5.0;
        check(g0 == sharedNormProviderGeneration(), "the generation reported with a snapshot is the current one");

        check(writeUserRow(*shipped.norm, editedMu), "a user norm set with the edited row is written");
        check(sharedNormProvider() == before,
              "WITHOUT a reset the take still returns the old set (why the save must reset it)");

        resetSharedNormProvider();
        check(sharedNormProviderGeneration() == g0 + 1, "the reset moves the generation by one");

        quint64                                    g1 = 0;
        const std::shared_ptr<const INormProvider> after = sharedNormProvider(&g1);
        check(after != nullptr && after != before, "after the reset the take is a NEW snapshot");
        check(g1 == g0 + 1, "…reported at the new generation");
        const NormResolution edited = after->resolve(kMeasure, kContext);
        check(edited.found() && edited.overridden && edited.norm->mu == editedMu,
              "…and it resolves the EDITED band, as the user's row");
        check(muOf(before) == shippedMu,
              "the snapshot taken before the reset still answers the shipped band (a reader mid-pass)");
        check(sharedNormProvider() == after, "a second take after the reset is the same snapshot (cached)");
    }

    std::printf("=== many first callers at once get ONE assembly ===\n");
    {
        resetSharedNormProvider();
        constexpr int                                     kThreads = 8;
        std::vector<std::shared_ptr<const INormProvider>> got(kThreads);
        runTogether(kThreads, [&got](int i) { got[size_t(i)] = sharedNormProvider(); });
        bool allSame = got[0] != nullptr;
        for (const auto &p : got) allSame = allSame && p == got[0];
        check(allSame, "eight threads racing the first build all hold the same provider");
        check(muOf(got[0]) == editedMu, "…and it is the set on disk");
    }

    std::printf("=== resets while other threads read ===\n");
    {
        const std::shared_ptr<const INormProvider> held = sharedNormProvider();
        const NormResolution                       row  = held->resolve(kMeasure, kContext);
        const Norm                                 base = *row.norm;
        const double                               alt  = shippedMu + 9.0;

        std::atomic<bool> stop{ false };
        std::atomic<int>  reads{ 0 }, nulls{ 0 }, unknown{ 0 }, regressed{ 0 };
        constexpr int     kReaders = 4;
        std::vector<QThread *> readers;
        for (int i = 0; i < kReaders; ++i) {
            readers.push_back(QThread::create([&]() {
                quint64 lastGen = 0;
                while (!stop.load()) {
                    quint64                                    gen = 0;
                    const std::shared_ptr<const INormProvider> p   = sharedNormProvider(&gen);
                    if (!p) { nulls.fetch_add(1); continue; }
                    // Read the snapshot more than once: it must not change under the reader.
                    const double a = muOf(p);
                    QThread::yieldCurrentThread();
                    const double b = muOf(p);
                    if (!(a == b && (a == editedMu || a == alt))) unknown.fetch_add(1);
                    if (gen < lastGen) regressed.fetch_add(1);
                    lastGen = gen;
                    reads.fetch_add(1);
                }
            }));
            readers.back()->start();
        }

        const quint64 g0      = sharedNormProviderGeneration();
        constexpr int kResets = 40;
        for (int k = 0; k < kResets; ++k) {
            // The file alternates between two values and the reset follows, as a save does.
            writeUserRow(base, (k % 2) ? editedMu : alt);
            resetSharedNormProvider();
            QThread::msleep(2);
        }
        stop.store(true);
        for (QThread *t : readers) {
            t->wait();
            delete t;
        }

        std::printf("      %d reads across %d resets\n", reads.load(), kResets);
        check(reads.load() > kReaders, "the readers really did read while the resets ran");
        check(nulls.load() == 0, "no reader was ever handed an empty provider");
        check(unknown.load() == 0,
              "every read was one of the two sets on disk, and a snapshot never changed under its reader");
        check(regressed.load() == 0, "no reader ever saw the generation go backwards");
        check(sharedNormProviderGeneration() == g0 + quint64(kResets), "each reset moved the generation once");
        check(muOf(held) == editedMu, "the snapshot held across all of it still answers what it answered");
        check(muOf(sharedNormProvider()) == editedMu, "the last write is what the app reads now");
    }

    // Leave nothing behind for the next run.
    QFile::remove(userNormPath());
    resetSharedNormProvider();
    check(muOf(sharedNormProvider()) == shippedMu, "with the user set removed, the shipped band is back");

    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
