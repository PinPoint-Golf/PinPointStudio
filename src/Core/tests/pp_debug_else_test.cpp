// ppDebug() takes no caller's `else` (src/Core/pp_debug.h; platform refactor A4).
//
// Below PINPOINT_DEBUG_LEVEL 3 the macro used to expand to `if (false) PpLogStream(…)`, so in
//     if (c) ppDebug() << 1; else hit = true;
// the `else` bound to the macro's hidden `if`: with c false nothing ran, with c true the else ran.
// This file is built twice (pp_debug_else_test_l1 at level 1, pp_debug_else_test_l3 at level 3) and
// proves at each level that the `else` belongs to the caller's `if`, and that the line runs at
// level 3 and is compiled away (its operands never evaluated) below it.
//
// The red: OLD_ppDebug() below is the old expansion, kept here so the hazard is shown in the same
// binary — at level 1 it takes the wrong branch both ways. Hand-rolled main, Qt Core only.

#include "pp_debug.h"

#include <cstdio>

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// The old expansion below level 3, verbatim.
#define OLD_ppDebug() if (false) PpLogStream(QtDebugMsg)

static int g_evaluated = 0;
static int sideEffect() { return ++g_evaluated; }

int main()
{
    std::printf("ppDebug() at PINPOINT_DEBUG_LEVEL %d\n", PINPOINT_DEBUG_LEVEL);

    for (const bool c : {false, true}) {
        bool hit = false;
        // The shape under test: deliberately unbraced.
        if (c) ppDebug() << 1; else hit = true;
        check(hit == !c, c ? "c true: the else is not taken" : "c false: the else is taken");
    }

    // Nested: the `else` still binds to the nearest caller's `if`.
    {
        bool outerElse = false, innerElse = false;
        const bool a = true, b = false;
        if (a)
            if (b) ppDebug() << "b"; else innerElse = true;
        else
            outerElse = true;
        check(innerElse && !outerElse, "nested: the else binds to the inner caller's if");
    }

    // Run at level 3 (a debug line goes to stderr only, never PpMessageLog, so the operand's side
    // effect is what shows it ran); compiled away below it, its operands never evaluated.
    g_evaluated = 0;
    ppDebug() << "pp_debug_else_test line" << sideEffect();
#if PINPOINT_DEBUG_LEVEL >= 3
    check(g_evaluated == 1, "level 3: the line runs and its operands are evaluated");
#else
    check(g_evaluated == 0, "below level 3: the line is compiled away, operands never evaluated");

    // The red: the old expansion captures the caller's else, both ways round.
#  if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdangling-else"
#  elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdangling-else"
#  endif
    for (const bool c : {false, true}) {
        bool hit = false;
        if (c) OLD_ppDebug() << 1; else hit = true;
        check(hit == c, c ? "old form, c true: wrongly takes the else (the hazard)"
                          : "old form, c false: wrongly skips the else (the hazard)");
    }
#  if defined(__clang__)
#    pragma clang diagnostic pop
#  elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#  endif
#endif

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
