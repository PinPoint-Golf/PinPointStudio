// SPDX-License-Identifier: GPL-3.0-or-later
// wt901_config_check.h — the connect-time ORIENT / AXIS6 / RRATE check (issue #105).
// The 27 Oct failure (ORIENT write not applied on one connection) cannot be made to
// happen at will on a real sensor, so the decision is pinned here against faked
// read-backs: confirmed, a wrong ORIENT that takes on a retry, one that never takes,
// silence, a wrong rate, and stray 0x71 frames.

#include <cstdint>
#include <cstdio>

#include "wt901_config_check.h"

using namespace pinpoint::wt901;
using V = ConfigCheck::Verdict;

static int g_fail = 0;

#define CHECK(label, cond)                                                       \
    do { bool ok = (cond);                                                       \
         std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                \
         if (!ok) ++g_fail; } while (0)

namespace {

constexpr uint16_t kRate100 = 0x09;

// One attempt's three read-backs; returns the verdict of the last.
V answer(ConfigCheck &c, uint16_t orient, uint16_t axis6, uint16_t rrate)
{
    c.onReadback(kRegOrient, orient);
    c.onReadback(kRegAxis6, axis6);
    return c.onReadback(kRegRRate, rrate);
}

void confirmedFirstTime()
{
    std::printf("confirmed on the first attempt\n");
    ConfigCheck c;
    c.begin(kRate100);
    CHECK("pending after ORIENT", c.onReadback(kRegOrient, 0x0001) == V::Pending);
    CHECK("pending after AXIS6",  c.onReadback(kRegAxis6, 0x0001) == V::Pending);
    CHECK("confirmed after RRATE", c.onReadback(kRegRRate, kRate100) == V::Confirmed);
    CHECK("attempt 1", c.attempt() == 1);
    CHECK("no longer active", !c.active());
    CHECK("a late timeout is ignored", c.onTimeout() == V::Pending);
}

void wrongOrientTakesOnRetry()
{
    std::printf("27 Oct: ORIENT not applied, then takes on the retry\n");
    ConfigCheck c;
    c.begin(kRate100);
    CHECK("retry after a horizontal ORIENT", answer(c, 0x0000, 0x0001, kRate100) == V::Retry);
    CHECK("ORIENT named wrong", c.wrongBits() == ConfigCheck::kBitOrient);
    c.nextAttempt();
    CHECK("attempt 2", c.attempt() == 2);
    CHECK("confirmed on attempt 2", answer(c, 0x0001, 0x0001, kRate100) == V::Confirmed);
}

void wrongOrientNeverTakes()
{
    std::printf("ORIENT never takes: Failed after kMaxAttempts\n");
    ConfigCheck c;
    c.begin(kRate100);
    V v = V::Pending;
    for (int i = 0; i < ConfigCheck::kMaxAttempts; ++i) {
        if (i > 0) c.nextAttempt();
        v = answer(c, 0x0000, 0x0001, kRate100);
        if (i + 1 < ConfigCheck::kMaxAttempts)
            CHECK("retry before the last attempt", v == V::Retry);
    }
    CHECK("failed on the last attempt", v == V::Failed);
    CHECK("attempt == kMaxAttempts", c.attempt() == ConfigCheck::kMaxAttempts);
    CHECK("inactive once failed", !c.active());
}

void wrongAxis6Fails()
{
    std::printf("AXIS6 stuck in 9-axis: Failed\n");
    ConfigCheck c;
    c.begin(kRate100);
    V v = V::Pending;
    for (int i = 0; i < ConfigCheck::kMaxAttempts; ++i) {
        if (i > 0) c.nextAttempt();
        v = answer(c, 0x0001, 0x0000, kRate100);
    }
    CHECK("failed", v == V::Failed);
    CHECK("AXIS6 named wrong", c.wrongBits() == ConfigCheck::kBitAxis6);
}

void silenceIsUnverified()
{
    std::printf("no read-backs at all: Unverified, never Failed\n");
    ConfigCheck c;
    c.begin(kRate100);
    CHECK("retry after the first silent attempt", c.onTimeout() == V::Retry);
    c.nextAttempt();
    CHECK("retry after the second", c.onTimeout() == V::Retry);
    c.nextAttempt();
    CHECK("unverified after the third", c.onTimeout() == V::Unverified);
    CHECK("all three missing", c.missingBits() == (ConfigCheck::kBitOrient | ConfigCheck::kBitAxis6
                                                   | ConfigCheck::kBitRRate));
}

void partialSilenceThenWrongOrient()
{
    std::printf("ORIENT wrong and RRATE silent on the last attempt: Failed\n");
    ConfigCheck c;
    c.begin(kRate100);
    c.onTimeout(); c.nextAttempt();
    c.onTimeout(); c.nextAttempt();
    c.onReadback(kRegOrient, 0x0000);
    c.onReadback(kRegAxis6, 0x0001);
    CHECK("failed on timeout", c.onTimeout() == V::Failed);
    CHECK("RRATE named missing", c.missingBits() == ConfigCheck::kBitRRate);
}

void wrongRateIsUnverified()
{
    std::printf("only RRATE wrong: Unverified (the frame is right)\n");
    ConfigCheck c;
    c.begin(0x0B);   // 200 Hz
    V v = V::Pending;
    for (int i = 0; i < ConfigCheck::kMaxAttempts; ++i) {
        if (i > 0) c.nextAttempt();
        v = answer(c, 0x0001, 0x0001, kRate100);
    }
    CHECK("unverified", v == V::Unverified);
    CHECK("RRATE named wrong", c.wrongBits() == ConfigCheck::kBitRRate);
}

void strayFramesIgnored()
{
    std::printf("stray and repeated 0x71 frames are not counted\n");
    ConfigCheck c;
    CHECK("not expecting before begin", !c.expecting(kRegOrient));
    CHECK("a read-back before begin is ignored", c.onReadback(kRegOrient, 0x0000) == V::Pending);
    c.begin(kRate100);
    CHECK("expecting ORIENT", c.expecting(kRegOrient));
    CHECK("other registers not expected", !c.expecting(0x64) && !c.expecting(0x01));
    c.onReadback(kRegOrient, 0x0001);
    CHECK("ORIENT answered once", !c.expecting(kRegOrient));
    CHECK("a second ORIENT is ignored", c.onReadback(kRegOrient, 0x0000) == V::Pending);
    CHECK("and does not mark it wrong", c.wrongBits() == 0);
    c.cancel();
    CHECK("cancel stops the check", !c.expecting(kRegAxis6) && c.onTimeout() == V::Pending);
}

void beginRestarts()
{
    std::printf("a reconnect restarts the count\n");
    ConfigCheck c;
    c.begin(kRate100);
    answer(c, 0x0000, 0x0001, kRate100);
    c.nextAttempt();
    c.begin(kRate100);
    CHECK("attempt back to 1", c.attempt() == 1);
    CHECK("nothing wrong carried over", c.wrongBits() == 0);
    CHECK("confirmed", answer(c, 0x0001, 0x0001, kRate100) == V::Confirmed);
}

} // namespace

int main()
{
    confirmedFirstTime();
    wrongOrientTakesOnRetry();
    wrongOrientNeverTakes();
    wrongAxis6Fails();
    silenceIsUnverified();
    partialSilenceThenWrongOrient();
    wrongRateIsUnverified();
    strayFramesIgnored();
    beginRestarts();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
