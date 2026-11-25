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

// MissedSwingNotice (#281's stopgap): a launch-monitor reading that arrives with no shot waiting, because its ball
// was struck while the last shot was still analysing, tells the golfer; a reading paired with its swing does not.
//
// Every offer comes from the real ShotPairing, in the order LaunchMonitorController makes them, and every notice
// lands in the real NotificationCenter. Case 1 is visit 1 (25 Nov 2026, step 3 log), on its own clock: swing 1 at
// 12:28:41, swing 2's reading 8a790d49 at 12:28:50 (the pipeline busy: told), swing 1 done at 12:28:57.5, swing 3's
// reading 82f81b3a at 12:28:58 (struck after the pipeline went idle; swing 3 WAS recorded: not told).

#include "app/notification_center.h"
#include "launchmonitor/missed_swing_notice.h"

#include <QCoreApplication>
#include <cstdio>

using pinpoint::lm::LaunchMonitorReading;
using pinpoint::lm::ShotPairing;

static int g_fail = 0;
static void checkInt(const char *label, long long got, long long want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-66s got %3lld  want %3lld\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}
static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-66s got %-5s want %-5s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

static qint64 g_now = 0;   // epoch ms; both the notice and the centre read it

// 12:28:SS on visit 1's day, as epoch ms (the date only offsets every value alike).
static qint64 at(double s) { return 1'795'000'000'000LL + qint64(s * 1000.0); }

static LaunchMonitorReading premier(const char *id, std::optional<qint64> contact)
{
    LaunchMonitorReading r;
    r.deviceKind   = QStringLiteral("premier");
    r.deviceShotId = QString::fromLatin1(id);
    r.contactAtMs  = contact;
    return r;
}
static LaunchMonitorReading fsx(const char *id)   // FSX2020's LastShot.CSV: no contact time
{
    LaunchMonitorReading r;
    r.deviceKind   = QStringLiteral("gcquad");
    r.deviceShotId = QString::fromLatin1(id);
    return r;
}

static const QString kId = QString::fromLatin1(MissedSwingNotice::kId);

// One rig: the pairing, the notice and the centre, wired as main.cpp and the controller wire them.
struct Rig {
    NotificationCenter nc;
    MissedSwingNotice  notice{ [] { return g_now; } };
    ShotPairing        pairing;
    bool               quiet = false;

    Rig()
    {
        nc.setClockForTest([] { return g_now; });
        notice.setSink(&nc);
        notice.setQuietWhen([this] { return quiet; });
    }
    // LaunchMonitorController::onReadingAvailable, the standalone path switched off.
    bool offer(const LaunchMonitorReading &r) { return notice.hear(pairing.offerReading(r), r); }
    int  told() const { return nc.isLive(kId) ? nc.countOf(kId) : 0; }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("=== MissedSwingNotice: a swing hit while the last one analysed is told ===\n");

    std::printf("\n-- 1. visit 1, step 3: swing 2 told, swing 1 and swing 3 not --\n");
    {
        Rig R;
        g_now = at(41.0);
        R.pairing.noteShotDetected(at(41.0));           // swing 1 committed
        R.notice.setPipelineBusy(true);                  // ShotProcessor::busyChanged
        g_now = at(42.0);
        checkBool("swing 1's own reading (contact Δ 34 ms) is parked, not told",
                  R.offer(premier("36e08446", at(41.034))), false);
        checkInt("nothing on screen", R.told(), 0);
        g_now = at(50.0);
        const ShotPairing::Offer o2 = R.pairing.offerReading(premier("8a790d49", at(49.0)));
        checkBool("swing 2's reading has no shot waiting (the pairing discards it)",
                  o2.disposition == ShotPairing::Disposition::Discarded, true);
        checkBool("…and the golfer is told", R.notice.hear(o2, premier("8a790d49", at(49.0))), true);
        checkInt("one notice", R.told(), 1);
        checkBool("its words", R.nc.data(R.nc.index(0, 0), NotificationCenter::TitleRole).toString()
                                   == MissedSwingNotice::title(), true);
        checkInt("a warning", R.nc.data(R.nc.index(0, 0), NotificationCenter::SeverityRole).toInt(),
                 NotificationCenter::Warn);
        checkInt("an event (it hides itself)", R.nc.data(R.nc.index(0, 0), NotificationCenter::KindRole).toInt(),
                 NotificationCenter::Event);
        g_now = at(57.5);
        R.pairing.noteSwingDir(QStringLiteral("/lib/s/swing_0001"));
        R.notice.setPipelineBusy(false);                 // finishShot: the buffer resumes
        g_now = at(58.0);
        checkBool("swing 3's reading, struck after the pipeline went idle, is discarded but not told",
                  R.offer(premier("82f81b3a", at(57.9))), false);
        checkInt("still the one notice", R.told(), 1);
    }

    std::printf("\n-- 2. a normal paired reading is never told --\n");
    {
        Rig R;
        R.notice.setPipelineBusy(true);
        R.pairing.noteShotDetected();
        checkBool("parked while the shot analyses", R.offer(fsx("101")), false);
        R.pairing.noteSwingDir(QStringLiteral("/lib/s/swing_0001"));
        R.pairing.noteShotDetected();
        R.pairing.noteSwingDir(QStringLiteral("/lib/s/swing_0002"));
        checkBool("claimed into a saved swing", R.offer(fsx("102")), false);
        checkInt("nothing on screen", R.told(), 0);
        checkBool("the same paired shot restated later, still busy, is not a missed swing",
                  R.offer(fsx("102")), false);
        checkInt("still nothing", R.told(), 0);
    }

    std::printf("\n-- 3. no contact time: told only while the pipeline is busy --\n");
    {
        Rig R;
        checkBool("discarded while idle (a ball hit with nothing armed): not told", R.offer(fsx("7")), false);
        R.notice.setPipelineBusy(true);
        checkBool("discarded while busy: told", R.offer(fsx("8")), true);
        checkBool("the same device shot again: not told twice", R.offer(fsx("8")), false);
        checkInt("one", R.told(), 1);
        checkBool("a second missed swing: told", R.offer(fsx("9")), true);
        checkInt("counted on the same notice: (×2)", R.told(), 2);
        checkBool("no id: never the same as another", R.offer(fsx("")), true);
        checkBool("…a second one with no id is told too", R.offer(fsx("")), true);
        checkInt("four", R.told(), 4);
    }

    std::printf("\n-- 4. a timed reading that lands after the pipeline went idle --\n");
    {
        Rig R;
        g_now = at(10.0); R.notice.setPipelineBusy(true);
        g_now = at(30.0); R.notice.setPipelineBusy(false);
        g_now = at(31.0);
        checkBool("struck at 29.5 s, inside the busy span, read at 31 s: told",
                  R.offer(premier("a", at(29.5))), true);
        checkBool("struck at 9 s, before the span: not told", R.offer(premier("b", at(9.0))), false);
        checkBool("struck at 30.4 s, after the span: not told", R.offer(premier("c", at(30.4))), false);
    }

    std::printf("\n-- 5. refused by its contact time while the next swing is armed --\n");
    {
        Rig R;
        g_now = at(0.0);  R.pairing.noteShotDetected(at(0.0));  R.notice.setPipelineBusy(true);
        g_now = at(20.0); R.pairing.noteShotFailed();           R.notice.setPipelineBusy(false);
        g_now = at(30.0); R.pairing.noteShotDetected(at(30.0)); R.notice.setPipelineBusy(true);
        g_now = at(31.0);
        const LaunchMonitorReading early = premier("e", at(12.0));   // struck during the first shot's analysis
        const ShotPairing::Offer o = R.pairing.offerReading(early);
        checkBool("the pairing refuses it as before our impact",
                  o.timeCheck == ShotPairing::TimeCheck::BeforeImpact, true);
        checkBool("struck inside the earlier busy span: told", R.notice.hear(o, early), true);
        const LaunchMonitorReading idle = premier("f", at(25.0));    // struck while nothing was busy
        checkBool("struck while idle: not told", R.offer(idle), false);
        const LaunchMonitorReading later = premier("g", at(40.0));   // the device dropped our shot; this is later
        const ShotPairing::Offer ol = R.pairing.offerReading(later);
        checkBool("the pairing refuses a later shot as after the window",
                  ol.timeCheck == ShotPairing::TimeCheck::AfterWindow, true);
        g_now = at(41.0);
        checkBool("struck while this shot analyses: told", R.notice.hear(ol, later), true);
    }

    std::printf("\n-- 6. never while a session is reviewed, and never for a reading saved as a shot of its own --\n");
    {
        Rig R;
        R.notice.setPipelineBusy(true);
        R.quiet = true;
        checkBool("reviewing a session: not told", R.offer(fsx("r1")), false);
        R.quiet = false;
        const LaunchMonitorReading s = fsx("s1");
        checkBool("saved as a standalone shot: not told", R.notice.hear(R.pairing.offerReading(s), s, true), false);
        checkBool("…nor when it is restated", R.offer(s), false);
        checkInt("nothing on screen", R.told(), 0);
    }

    std::printf("\n-- 7. no sink: decided, nothing posted --\n");
    {
        MissedSwingNotice n([] { return g_now; });
        n.setPipelineBusy(true);
        ShotPairing p;
        const LaunchMonitorReading r = fsx("x");
        checkBool("returns false with nowhere to tell", n.hear(p.offerReading(r), r), false);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
