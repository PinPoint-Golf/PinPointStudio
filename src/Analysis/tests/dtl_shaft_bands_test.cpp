// Standalone test for the sighted-band rule (src/Analysis/dtl_shaft_bands.h): the §5.8
// minimum and the band-EDGE rule of dtl_continuous_track_design_update.md §3.1. Pure
// std over a sighted / end-on / well-sighted pattern, no frame, no witness.
//
//   cmake --build build/tests --target dtl_shaft_bands_test
//   ctest --test-dir build/tests -R dtl_shaft_bands_test --output-on-failure
#include "../dtl_shaft_bands.h"
#include <cstdio>
#include <string>
using namespace pinpoint::analysis::dtlbands;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
// pattern: 'S' sighted, 'E' end-on (not sighted), 'q' quarantined (not sighted, not end-on),
// 'W' sighted AND well-sighted (ρ̂ over the edge margin)
struct Pat {
    std::vector<char> sighted, endOn, rhoOk;
    explicit Pat(const std::string &p)
    {
        for (char c : p) {
            sighted.push_back(c == 'S' || c == 'W');
            endOn.push_back(c == 'E');
            rhoOk.push_back(c == 'W');
        }
    }
};
static std::string spell(const Result &r, size_t n)
{
    std::string s(n, '.');
    for (const Run &b : r.bands) for (int k = b.lo; k <= b.hi; ++k) s[size_t(k)] = b.edge ? 'e' : 'B';
    return s;
}

int main()
{
    std::printf("dtl_shaft_bands_test\n");
    // §1 the original rule: a run shorter than six is refused, full runs are bands
    {
        const Pat p("SSSSSSSSEEESSSEEE");
        const Result r = sightedRuns(p.sighted, p.endOn, 6, false, 3, 2, p.rhoOk);
        check(r.bands.size() == 1 && r.bands[0].lo == 0 && r.bands[0].hi == 7 && !r.bands[0].edge,
              "§1 the eight-frame run is a band");
        check(r.refused.size() == 1 && r.refused[0].lo == 11 && r.refused[0].hi == 13,
              "§1 the three-frame run is refused with the edge rule off");
        std::printf("       %s\n", spell(r, p.sighted.size()).c_str());
    }
    // §2 ground (a): a short run across a quarantine hole next to a full band is admitted
    {
        const Pat p("SSSSSSSSqqSSSEEE");
        const Result r = sightedRuns(p.sighted, p.endOn, 6, true, 3, 2, p.rhoOk);
        check(r.bands.size() == 2 && r.bands[1].edge && r.bands[1].lo == 10 && r.bands[1].hi == 12,
              "§2 a 3-frame run 2 quarantined frames after a full band is an edge band");
        check(r.refused.empty(), "§2 nothing refused");
        std::printf("       %s\n", spell(r, p.sighted.size()).c_str());
        const Pat q("SSSSSSSSqqqSSSEEE");
        const Result r2 = sightedRuns(q.sighted, q.endOn, 6, true, 3, 2, q.rhoOk);
        check(r2.bands.size() == 1 && r2.refused.size() == 1, "§2 three quarantined frames is too wide a hole");
        const Pat e("SSSSSSSSEqSSSEEE");
        const Result r3 = sightedRuns(e.sighted, e.endOn, 6, true, 3, 2, e.rhoOk);
        check(r3.bands.size() == 1 && r3.refused.size() == 1, "§2 an end-on frame in the hole blocks it — never across an end-on gap");
        const Pat s("SSSSSSSSqqSSEEE");
        const Result r4 = sightedRuns(s.sighted, s.endOn, 6, true, 3, 2, s.rhoOk);
        check(r4.bands.size() == 1 && r4.refused.size() == 1, "§2 a 2-frame run is below the edge minimum");
    }
    // §3 ground (b): a short run well sighted in its own right — 07-04 s7's impact band
    {
        const Pat p("EEEEWWWWWEEEWWWWWEEEE");
        const Result r = sightedRuns(p.sighted, p.endOn, 6, true, 3, 2, p.rhoOk);
        check(r.bands.size() == 2 && r.bands[0].edge && r.bands[1].edge
              && r.bands[0].lo == 4 && r.bands[0].hi == 8 && r.bands[1].lo == 12 && r.bands[1].hi == 16,
              "§3 two five-frame runs at ρ̂ 0.95 separated by end-on flicker are two edge bands");
        std::printf("       %s\n", spell(r, p.sighted.size()).c_str());
        const Pat m("EEEESWSSSEEE");      // median not over the margin (1 of 5)
        const Result r2 = sightedRuns(m.sighted, m.endOn, 6, true, 3, 2, m.rhoOk);
        check(r2.bands.empty() && r2.refused.size() == 1, "§3 a run whose ρ̂ median sits at the threshold is still refused");
        const Pat h("EEEESWWSSEEE");      // 2 of 5: not a majority
        const Result r3 = sightedRuns(h.sighted, h.endOn, 6, true, 3, 2, h.rhoOk);
        check(r3.bands.empty(), "§3 two of five is not a median over the margin");
        const Pat k("EEEESWWWSEEE");      // 3 of 5: the median clears it
        const Result r4 = sightedRuns(k.sighted, k.endOn, 6, true, 3, 2, k.rhoOk);
        check(r4.bands.size() == 1 && r4.bands[0].edge, "§3 three of five is");
        const Result off = sightedRuns(p.sighted, p.endOn, 6, true, 3, 2, {});
        check(off.bands.empty(), "§3 with no ρ̂ column ground (b) is inert");
    }
    // §4 chains: short runs do not admit each other; a full band decides
    {
        const Pat p("SSSqqSSSqqSSSSSSSS");
        const Result r = sightedRuns(p.sighted, p.endOn, 6, true, 3, 2, p.rhoOk);
        check(r.bands.size() == 2 && r.bands[0].edge && r.bands[0].lo == 5 && !r.bands[1].edge,
              "§4 only the short run touching the full band is admitted; the one beyond it is not");
        check(r.refused.size() == 1 && r.refused[0].lo == 0, "§4 … and is refused");
        std::printf("       %s\n", spell(r, p.sighted.size()).c_str());
    }
    // §5 ordering and emptiness
    {
        const Pat p("EEEE");
        const Result r = sightedRuns(p.sighted, p.endOn, 6, true, 3, 2, p.rhoOk);
        check(r.bands.empty() && r.refused.empty(), "§5 nothing sighted ⇒ nothing");
        const Pat q("SSSSSSSqqSSSqqSSSSSSSS");
        const Result r2 = sightedRuns(q.sighted, q.endOn, 6, true, 3, 2, q.rhoOk);
        bool sorted = true;
        for (size_t i = 1; i < r2.bands.size(); ++i) sorted = sorted && r2.bands[i].lo > r2.bands[i - 1].hi;
        check(sorted && r2.bands.size() == 3, "§5 bands come back in frame order, the middle one an edge of either neighbour");
    }
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
