// Standalone tests for the launch monitor club evidence rule (src/Analysis/
// lm_club_evidence.h): every threshold at its boundary, two votes decide and a split
// decides nothing, a missing reading abstains, who is checked (driver, irons, wedges — never
// fairway woods, hybrids or unknown labels), the suggestion is the session's most common
// recorded club of the other family with a bag-order tie-break, and a generic suggestion
// when the session holds none. Then the 4 Aug 2026 Wrist_05 fixture, its 25 shots' raw
// readings transcribed from the documents, with every verdict pinned.
// Pure — QtCore only, no fixture on disk. Own main()/check() macros.
//
//   cmake --build build/tests --target lm_club_evidence_test
//   ctest --test-dir build/tests -R lm_club_evidence --output-on-failure

#include "../lm_club_evidence.h"

#include <cstdio>
#include <limits>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void checkStr(const QString &got, const QString &want, const char *label)
{
    const bool ok = got == want;
    std::printf("  [%s] %s (got \"%s\", want \"%s\")\n", ok ? "PASS" : "FAIL", label,
                qPrintable(got), qPrintable(want));
    if (!ok) ++g_fail;
}

static LmEvidenceShot shot(const char *club, std::optional<double> loft,
                           std::optional<double> smash, std::optional<double> spin)
{
    return LmEvidenceShot{ QString::fromUtf8(club), loft, smash, spin };
}

int main()
{
    std::printf("lm_club_evidence_test\n");
    const LmClubEvidenceRule R;

    // ── Each test at its boundaries ─────────────────────────────────────────────
    {
        auto loft  = [](double v) { return lmClubVotes(v, std::nullopt, std::nullopt); };
        auto smash = [](double v) { return lmClubVotes(std::nullopt, v, std::nullopt); };
        auto spin  = [](double v) { return lmClubVotes(std::nullopt, std::nullopt, v); };
        check(loft(17.0).wood == 1, "loft 17.0 votes wood (≤ 17)");
        check(loft(17.01).wood == 0 && loft(17.01).iron == 0, "loft 17.01 abstains");
        check(loft(19.99).wood == 0 && loft(19.99).iron == 0, "loft 19.99 abstains");
        check(loft(20.0).iron == 1, "loft 20.0 votes iron (≥ 20)");
        check(smash(1.42).wood == 1, "smash 1.42 votes wood (≥ 1.42)");
        check(smash(1.41).wood == 0 && smash(1.41).iron == 0, "smash 1.41 abstains");
        check(smash(1.40).iron == 1, "smash 1.40 votes iron (≤ 1.40)");
        check(spin(3500).wood == 1, "spin 3500 votes wood (≤ 3500)");
        check(spin(3750).wood == 0 && spin(3750).iron == 0, "spin 3750 abstains");
        check(spin(4000).iron == 1, "spin 4000 votes iron (≥ 4000)");
        check(loft(17.0).of == 1 && lmClubVotes(std::nullopt, std::nullopt, std::nullopt).of == 0,
              "`of` counts the tests that had a reading");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        check(lmClubVotes(nan, nan, nan).of == 0, "a NaN reading abstains and is not counted");
    }

    // ── Two votes decide; a split decides nothing ───────────────────────────────
    {
        check(lmReadsLike(lmClubVotes(12.0, 1.45, std::nullopt)) == LmClubFamily::Wood, "two wood votes read wood");
        check(lmReadsLike(lmClubVotes(30.0, 1.30, std::nullopt)) == LmClubFamily::Iron, "two iron votes read iron");
        check(lmReadsLike(lmClubVotes(12.0, std::nullopt, std::nullopt)) == LmClubFamily::None, "one vote decides nothing");
        check(lmReadsLike(lmClubVotes(12.0, 1.30, std::nullopt)) == LmClubFamily::None, "one each way decides nothing");
        check(lmReadsLike(lmClubVotes(18.0, 1.30, 2000)) == LmClubFamily::None, "1–1 with an abstention decides nothing");
        check(lmReadsLike(lmClubVotes(30.0, 1.30, 2000)) == LmClubFamily::Iron, "2–1 decides");
    }

    // ── Labels: heads, who is checked, who is a candidate ───────────────────────
    {
        checkStr(lmClubHead(QStringLiteral("DRIVER")), QStringLiteral("driver"), "DRIVER → driver");
        checkStr(lmClubHead(QStringLiteral("3 WOOD")), QStringLiteral("3wood"), "3 WOOD → 3wood");
        checkStr(lmClubHead(QStringLiteral("5 WOOD")), QStringLiteral("3wood"), "5 WOOD → 3wood (one fairway render)");
        checkStr(lmClubHead(QStringLiteral("4 HYBRID")), QStringLiteral("hybrid"), "4 HYBRID → hybrid");
        checkStr(lmClubHead(QStringLiteral("7 IRON")), QStringLiteral("iron"), "7 IRON → iron");
        checkStr(lmClubHead(QStringLiteral("2 iron")), QStringLiteral("iron"), "2 iron → iron");
        checkStr(lmClubHead(QStringLiteral("PITCHING WEDGE")), QStringLiteral("wedge"), "PITCHING WEDGE → wedge");
        checkStr(lmClubHead(QStringLiteral("PW")), QStringLiteral("wedge"), "PW → wedge");
        checkStr(lmClubHead(QStringLiteral("56°")), QStringLiteral("wedge"), "56° → wedge");
        checkStr(lmClubHead(QStringLiteral("7i")), QStringLiteral("iron"), "7i → iron");
        checkStr(lmClubHead(QStringLiteral("3w")), QStringLiteral("3wood"), "3w → 3wood");
        checkStr(lmClubHead(QString()), QStringLiteral("iron"), "no club draws as the iron");

        check(lmCheckedFamily(QStringLiteral("DRIVER")) == LmClubFamily::Wood, "the driver is checked as a wood");
        check(lmCheckedFamily(QStringLiteral("7 IRON")) == LmClubFamily::Iron, "an iron is checked as an iron");
        check(lmCheckedFamily(QStringLiteral("SAND WEDGE")) == LmClubFamily::Iron, "a wedge is checked as an iron");
        check(lmCheckedFamily(QStringLiteral("5 WOOD")) == LmClubFamily::None, "a fairway wood is never checked");
        check(lmCheckedFamily(QStringLiteral("4 HYBRID")) == LmClubFamily::None, "a hybrid is never checked");
        check(lmCheckedFamily(QStringLiteral("PUTTER")) == LmClubFamily::None, "an unknown label is never checked");
        check(lmCandidateFamily(QStringLiteral("3 WOOD")) == LmClubFamily::Wood, "a fairway wood IS a wood candidate");
        check(lmCandidateFamily(QStringLiteral("4 HYBRID")) == LmClubFamily::None, "a hybrid is nobody's candidate");
    }

    // ── A 5 wood is not "corrected" to an iron ──────────────────────────────────
    {
        // 22° dynamic loft, 1.44 smash, 4600 rpm: two iron votes. A real 5 wood.
        const auto ev = lmClubEvidence({ shot("5 WOOD", 22.0, 1.44, 4600), shot("7 IRON", 28.0, 1.33, 5200) });
        check(ev[0].reads == LmClubFamily::Iron, "the 5 wood's readings do read iron-like…");
        check(!ev[0].mismatch && ev[0].suggest.isEmpty(), "…and it is still not flagged, because woods are not checked");
    }

    // ── Suggestions: most common of the other family; bag order on a tie; generic ──
    {
        const auto ev = lmClubEvidence({
            shot("DRIVER", 30.0, 1.30, 5000),     // reads iron
            shot("8 IRON", 33.0, 1.30, 6000),
            shot("7 IRON", 30.0, 1.33, 5200),
            shot("8 IRON", 33.0, 1.30, 6000),
            shot("7 IRON", 30.0, 1.33, 5200),
        });
        check(ev[0].mismatch && ev[0].specific, "a driver shot reading iron is a specific mismatch");
        checkStr(ev[0].suggest, QStringLiteral("7 IRON"), "two each of 7 and 8 iron: the tie goes to the 7 (earlier in the bag)");
        check(ev[0].votes == 3 && ev[0].of == 3, "votes and of: 3 of 3");

        const auto lone = lmClubEvidence({ shot("DRIVER", 30.0, 1.30, 5000), shot("DRIVER", 11.0, 1.48, 2500) });
        check(lone[0].mismatch && !lone[0].specific, "no iron in the session: flagged, not relabelled");
        checkStr(lone[0].suggest, QStringLiteral("an iron"), "the generic suggestion");
        check(!lone[1].mismatch, "a driver reading like a driver is not flagged");

        const auto woodish = lmClubEvidence({ shot("7 IRON", 12.0, 1.46, 2600), shot("3 WOOD", 14.0, 1.45, 3200),
                                              shot("7 IRON", 30.0, 1.33, 5200) });
        check(woodish[0].mismatch && woodish[0].specific, "a 7-iron shot reading wood, in a session with a 3 wood");
        checkStr(woodish[0].suggest, QStringLiteral("3 WOOD"), "the 3 wood is the wood candidate");
        const auto noWood = lmClubEvidence({ shot("7 IRON", 12.0, 1.46, 2600) });
        checkStr(noWood[0].suggest, QStringLiteral("a wood"), "no wood in the session: \"a wood\"");
    }

    // ── The fixture: 2026-08-04 Mark-Liversedge Wrist_05, 25 GC Quad shots ───────
    //
    // Raw doubles from each swing document's lm.dynamicLoft / lm.smashFactor / lm.spinRate at
    // Impact, recorded club from review.club. Shot numbers are the swing indices (3 is gone).
    // Every one of these shots was hit with the 7 iron; Driver was left selected on the
    // device for shots 6–13. The rule is per shot, and it catches four of the eight: three
    // read genuinely driver-like (low loft or high smash, with driver spin), and one splits.
    {
        struct Row { int no; const char *club; std::optional<double> loft, smash, spin; };
        const std::optional<double> none;
        const Row rows[] = {
            {  1, "7 IRON", 31.848963, 1.155739, 3649 }, {  2, "7 IRON", 28.988005, 1.347398, 5699 },
            {  4, "7 IRON", none, none, 3064 },          {  5, "7 IRON", none, none, 3424 },
            {  6, "DRIVER", 18.072773, 1.366184, 2189 }, {  7, "DRIVER", 12.701368, 1.343509, 2999 },
            {  8, "DRIVER", 19.286068, 1.443881, 2374 }, {  9, "DRIVER", 35.127781, 1.361578, 2284 },
            { 10, "DRIVER", 24.208738, 1.308598, 2664 }, { 11, "DRIVER", 17.521824, 1.450416, 3034 },
            { 12, "DRIVER", 35.668701, 1.350684, 1964 }, { 13, "DRIVER", 29.198549, 1.302264, 4949 },
            { 14, "7 IRON", 29.411379, 1.305290, 4814 }, { 15, "7 IRON", 26.436426, 1.362161, 4534 },
            { 16, "7 IRON", 26.210945, 1.351983, 4789 }, { 17, "7 IRON", 25.570358, 1.371868, 4749 },
            { 18, "7 IRON", 29.681005, 1.320563, 4514 }, { 19, "7 IRON", 28.932661, 1.334753, 5749 },
            { 20, "7 IRON", 26.163641, 1.336344, 5464 }, { 21, "7 IRON", 24.117765, 1.386829, 4814 },
            { 22, "7 IRON", 25.782841, 1.385487, 5000 }, { 23, "7 IRON", 29.199259, 1.323617, 4634 },
            { 24, "7 IRON", 28.287287, 1.335489, 4834 }, { 25, "7 IRON", 25.548281, 1.388463, 4824 },
            { 26, "7 IRON", 24.567158, 1.372897, 4899 },
        };
        std::vector<LmEvidenceShot> in;
        for (const Row &r : rows) in.push_back(shot(r.club, r.loft, r.smash, r.spin));
        const auto ev = lmClubEvidence(in);

        int flagged = 0;
        for (size_t i = 0; i < ev.size(); ++i) {
            const LmClubEvidence &e = ev[i];
            std::printf("    #%-2d %-6s wood %d iron %d of %d → %-4s %s\n", rows[i].no, rows[i].club,
                        e.tally.wood, e.tally.iron, e.of, qPrintable(lmClubFamilyName(e.reads)),
                        e.mismatch ? qPrintable(QStringLiteral("suggest ") + e.suggest) : "");
            if (e.mismatch) ++flagged;
        }
        auto at = [&](int no) -> const LmClubEvidence & {
            for (size_t i = 0; i < ev.size(); ++i) if (rows[i].no == no) return ev[i];
            return ev[0];
        };
        check(flagged == 4, "four shots are flagged");
        for (int no : { 9, 10, 12, 13 }) {
            const LmClubEvidence &e = at(no);
            char label[96];
            std::snprintf(label, sizeof label, "#%d (recorded Driver) reads iron → 7 IRON, relabelled", no);
            check(e.mismatch && e.specific && e.suggest == QLatin1String("7 IRON")
                  && e.reads == LmClubFamily::Iron, label);
        }
        for (int no : { 7, 8, 11 }) {
            const LmClubEvidence &e = at(no);
            char label[96];
            std::snprintf(label, sizeof label, "#%d (recorded Driver) reads wood: no mismatch", no);
            check(e.reads == LmClubFamily::Wood && e.votes == 2 && !e.mismatch, label);
        }
        check(at(6).reads == LmClubFamily::None && at(6).tally.wood == 1 && at(6).tally.iron == 1,
              "#6 (recorded Driver) splits 1–1 with loft abstaining at 18.1°: no verdict");
        check(at(4).reads == LmClubFamily::None && at(4).of == 1, "#4: spin only, one vote, no verdict");
        bool ironsClean = true;
        for (size_t i = 0; i < ev.size(); ++i)
            if (QString::fromUtf8(rows[i].club) == QLatin1String("7 IRON") && ev[i].mismatch) ironsClean = false;
        check(ironsClean, "no 7-iron shot is flagged");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
