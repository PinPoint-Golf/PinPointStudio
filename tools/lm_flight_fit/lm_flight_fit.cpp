// lm_flight_fit — refit LmFlightModel's aero constants against launch monitor shots.
//
// Reads the CSV extract_gcquad_shots.py writes, integrates every shot through the SAME
// lmIntegrateFlight() the panel uses (the header is included, not re-implemented, so the
// fit cannot drift from the model it tunes), and minimises the weighted mean squared
// RELATIVE error in carry, apex height and descent angle over cd0, cdS, clS, clS2 and clMax.
//
// Plausibility is enforced, not hoped for: Cd must stay within 0.20–0.35 over S = 0…0.4,
// the lift curve's peak must sit inside S = 0.15…0.60 (beyond it Cl is held — see
// LmFlightModel::lift()), and clMax within 0.15…0.40. A candidate outside any of those
// costs a penalty that dwarfs any fit gain. Nelder–Mead from several starts; the best wins.
//
//   c++ -O2 -std=c++17 -I src/Analysis tools/lm_flight_fit/lm_flight_fit.cpp -o <out>/lm_flight_fit
//   <out>/lm_flight_fit shots.csv                      # report the compiled defaults, fit, report
//   <out>/lm_flight_fit shots.csv --eval cd0 cdS clS clS2 clMax   # report one constant set only
//   ... --uniform                                      # every shot weighted alike (see README)
//
// See README.md beside this file.

#include "../../src/LaunchMonitor/lm_flight_path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace pinpoint::analysis;

namespace {

struct Shot {
    std::string session, swing;
    LmLaunch in;
    double carry = 0, apex = 0, descent = 0;
    double weight = 1.0;
};

// Weight the low-spin (< 3000 rpm) shots and the rest equally IN TOTAL, rather than shot
// by shot, so a corpus that is mostly mid-spin irons cannot outvote the few low-spin long
// shots — which are exactly the shots the old constants under-carried. The default;
// --uniform weights every shot alike instead. The README says what the choice costs.
void balanceWeights(std::vector<Shot> &shots)
{
    size_t low = 0;
    for (const Shot &s : shots) if (s.in.spinRpm < 3000.0) ++low;
    const size_t high = shots.size() - low;
    if (low == 0 || high == 0) return;
    for (Shot &s : shots)
        s.weight = 0.5 * double(shots.size()) / double(s.in.spinRpm < 3000.0 ? low : high);
}

std::vector<std::string> splitCsv(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (char c : line) {
        if (c == '"') q = !q;
        else if (c == ',' && !q) { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<Shot> readShots(const char *path)
{
    std::ifstream f(path);
    std::string line;
    std::vector<Shot> shots;
    if (!std::getline(f, line)) return shots;
    std::map<std::string, size_t> col;
    const auto head = splitCsv(line);
    for (size_t i = 0; i < head.size(); ++i) col[head[i]] = i;
    const auto num = [&](const std::vector<std::string> &r, const char *k) {
        const auto it = col.find(k);
        return (it == col.end() || it->second >= r.size() || r[it->second].empty())
                   ? 0.0 : std::atof(r[it->second].c_str());
    };
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const auto r = splitCsv(line);
        Shot s;
        s.session = r[col["session"]];
        s.swing   = r[col["swing"]];
        s.in = { num(r, "ballSpeed"), num(r, "launchAngle"), num(r, "launchDirection"),
                 num(r, "spinRate"),  num(r, "spinAxis") };
        s.carry   = num(r, "carryDistance");
        s.apex    = num(r, "peakHeight");
        s.descent = num(r, "descentAngle");
        shots.push_back(s);
    }
    return shots;
}

using Params = std::array<double, 5>;   // cd0, cdS, clS, clS2, clMax

LmFlightModel modelOf(const Params &p)
{
    LmFlightModel m;
    m.cd0 = p[0]; m.cdS = p[1]; m.clS = p[2]; m.clS2 = p[3]; m.clMax = p[4];
    return m;
}

double plausibilityPenalty(const Params &p)
{
    double pen = 0.0;
    const auto band = [&](double v, double lo, double hi) {
        if (v < lo) pen += (lo - v) * (lo - v);
        if (v > hi) pen += (v - hi) * (v - hi);
    };
    band(p[0], 0.20, 0.35);                 // Cd at S = 0
    band(p[0] + p[1] * 0.4, 0.20, 0.35);    // Cd at S = 0.4
    band(p[1], 0.0, 1.0);                   // drag never falls with spin
    band(p[2], 0.0, 10.0);
    band(p[3], 0.0, 20.0);
    const double sPeak = p[3] > 1e-9 ? p[2] / (2.0 * p[3]) : 1.0;
    band(sPeak, 0.15, 0.60);
    band(p[4], 0.15, 0.40);
    return pen * 1e4;
}

struct Errors { double dCarry, dApex, dDescent; bool ok; };

Errors errorsFor(const Shot &s, const LmFlightModel &m)
{
    const LmFlightIntegration f = lmIntegrateFlight(s.in, m);
    if (!f.has) return { 0, 0, 0, false };
    return { f.carryYd - s.carry, f.apexFt - s.apex, f.descentDeg - s.descent, true };
}

// The data term alone: the weighted mean of the three squared relative errors.
double dataCost(const std::vector<Shot> &shots, const Params &p)
{
    const LmFlightModel m = modelOf(p);
    double c = 0.0;
    for (const Shot &s : shots) {
        const Errors e = errorsFor(s, m);
        if (!e.ok) return 1e6;
        const double rc = e.dCarry / s.carry, ra = e.dApex / s.apex, rd = e.dDescent / s.descent;
        c += s.weight * (rc * rc + ra * ra + rd * rd);
    }
    return c / double(shots.size());
}

double cost(const std::vector<Shot> &shots, const Params &p)
{
    const double pen = plausibilityPenalty(p);
    if (pen > 1.0) return 1e6 + pen;        // do not even integrate a nonsense candidate
    return dataCost(shots, p) + pen;
}

Params nelderMead(const std::vector<Shot> &shots, Params x0, int iters)
{
    const int n = 5;
    std::vector<Params> simplex(n + 1, x0);
    std::vector<double> fv(n + 1);
    for (int i = 0; i < n; ++i)
        simplex[i + 1][i] += std::max(0.05 * std::fabs(x0[i]), 0.01);
    for (int i = 0; i <= n; ++i) fv[i] = cost(shots, simplex[i]);

    for (int it = 0; it < iters; ++it) {
        std::vector<int> idx(n + 1);
        for (int i = 0; i <= n; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return fv[a] < fv[b]; });
        const int best = idx[0], worst = idx[n], second = idx[n - 1];
        if (std::fabs(fv[worst] - fv[best]) < 1e-12) break;

        Params cen{};
        for (int k = 0; k < n; ++k) {
            const int i = idx[k];
            for (int d = 0; d < n; ++d) cen[d] += simplex[i][d] / n;
        }
        const auto along = [&](double t) {
            Params p;
            for (int d = 0; d < n; ++d) p[d] = cen[d] + t * (simplex[worst][d] - cen[d]);
            return p;
        };
        const Params xr = along(-1.0);
        const double fr = cost(shots, xr);
        if (fr < fv[best]) {
            const Params xe = along(-2.0);
            const double fe = cost(shots, xe);
            if (fe < fr) { simplex[worst] = xe; fv[worst] = fe; }
            else         { simplex[worst] = xr; fv[worst] = fr; }
        } else if (fr < fv[second]) {
            simplex[worst] = xr; fv[worst] = fr;
        } else {
            const Params xc = along(fr < fv[worst] ? -0.5 : 0.5);
            const double fc = cost(shots, xc);
            if (fc < std::min(fr, fv[worst])) { simplex[worst] = xc; fv[worst] = fc; }
            else {
                for (int i = 0; i <= n; ++i) {
                    if (i == best) continue;
                    for (int d = 0; d < n; ++d)
                        simplex[i][d] = simplex[best][d] + 0.5 * (simplex[i][d] - simplex[best][d]);
                    fv[i] = cost(shots, simplex[i]);
                }
            }
        }
    }
    int best = 0;
    for (int i = 1; i <= n; ++i) if (fv[i] < fv[best]) best = i;
    return simplex[best];
}

double median(std::vector<double> v)
{
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    const size_t k = v.size() / 2;
    return v.size() % 2 ? v[k] : 0.5 * (v[k - 1] + v[k]);
}

void report(const char *title, const std::vector<Shot> &shots, const Params &p)
{
    const LmFlightModel m = modelOf(p);
    std::printf("\n== %s ==\n", title);
    const double pen = plausibilityPenalty(p);
    std::printf("cd0 %.4f  cdS %.4f  clS %.4f  clS2 %.4f  clMax %.4f   (fit cost %.6f%s)\n",
                p[0], p[1], p[2], p[3], p[4], dataCost(shots, p),
                pen > 0.0 ? ", OUTSIDE the plausibility box" : "");
    // Where the lift curve tops out. Computed here rather than through LmFlightModel so the
    // tool still builds against a header from before the lift curve was held at its peak.
    const double sPeak = p[3] > 1e-9 ? p[2] / (2.0 * p[3]) : INFINITY;
    const double clPeak = std::isfinite(sPeak) ? std::min(p[2] * sPeak - p[3] * sPeak * sPeak, p[4]) : p[4];
    std::printf("Cd(S=0) %.3f  Cd(S=0.4) %.3f  lift peak at S %.3f, Cl there %.3f (clMax %.3f)\n",
                p[0], p[0] + 0.4 * p[1], sPeak, clPeak, p[4]);

    struct Band { const char *name; double lo, hi; };
    const Band bands[] = { { "all", 0, 1e9 }, { "<3000", 0, 3000 },
                           { "3000-6000", 3000, 6000 }, { ">6000", 6000, 1e9 } };
    std::printf("%-10s %3s | %-21s | %-21s | %-21s\n", "spin", "n",
                "carry err yd med/worst", "apex err ft med/worst", "descent err deg med/worst");
    for (const Band &b : bands) {
        std::vector<double> c, a, d, cs, as, ds;
        for (const Shot &s : shots) {
            if (s.in.spinRpm < b.lo || s.in.spinRpm >= b.hi) continue;
            const Errors e = errorsFor(s, m);
            c.push_back(std::fabs(e.dCarry)); a.push_back(std::fabs(e.dApex));
            d.push_back(std::fabs(e.dDescent));
            cs.push_back(e.dCarry); as.push_back(e.dApex); ds.push_back(e.dDescent);
        }
        if (c.empty()) { std::printf("%-10s %3d | (no shots)\n", b.name, 0); continue; }
        // Worst is reported signed: which way the model misses matters as much as how far.
        const auto worstSigned = [](const std::vector<double> &v) {
            double w = 0; for (double x : v) if (std::fabs(x) > std::fabs(w)) w = x; return w;
        };
        std::printf("%-10s %3zu | %6.1f  %+7.1f       | %6.1f  %+7.1f       | %6.1f  %+7.1f\n",
                    b.name, c.size(), median(c), worstSigned(cs), median(a), worstSigned(as),
                    median(d), worstSigned(ds));
    }
    std::printf("median signed error, all: carry %+.1f yd, apex %+.1f ft, descent %+.1f deg\n",
                [&] { std::vector<double> v; for (const Shot &s : shots) v.push_back(errorsFor(s, m).dCarry); return median(v); }(),
                [&] { std::vector<double> v; for (const Shot &s : shots) v.push_back(errorsFor(s, m).dApex); return median(v); }(),
                [&] { std::vector<double> v; for (const Shot &s : shots) v.push_back(errorsFor(s, m).dDescent); return median(v); }());

    std::printf("per shot: session swing | mph launch rpm axis | carry model/dev | apex model/dev | descent model/dev\n");
    for (const Shot &s : shots) {
        const LmFlightIntegration f = lmIntegrateFlight(s.in, m);
        std::printf("  %s %s | %5.1f %4.1f %5.0f %5.1f | %6.1f %6.1f | %5.1f %5.1f | %4.1f %4.1f\n",
                    s.session.c_str(), s.swing.c_str(), s.in.ballSpeedMph, s.in.launchAngleDeg,
                    s.in.spinRpm, s.in.spinAxisDeg, f.carryYd, s.carry, f.apexFt, s.apex,
                    f.descentDeg, s.descent);
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s shots.csv [--eval cd0 cdS clS clS2 clMax]\n", argv[0]);
        return 2;
    }
    std::vector<Shot> shots = readShots(argv[1]);
    if (shots.empty()) { std::fprintf(stderr, "no shots in %s\n", argv[1]); return 1; }
    std::printf("%zu shots from %s\n", shots.size(), argv[1]);
    bool uniform = false;
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--uniform") uniform = true;
    if (!uniform) balanceWeights(shots);
    std::printf("%s weights\n", uniform ? "uniform" : "spin-balanced");

    const LmFlightModel def;
    const Params compiled{ def.cd0, def.cdS, def.clS, def.clS2, def.clMax };

    if (argc >= 8 && std::string(argv[2]) == "--eval") {
        const Params p{ std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5]),
                        std::atof(argv[6]), std::atof(argv[7]) };
        report("given constants", shots, p);
        return 0;
    }

    report("compiled defaults (before)", shots, compiled);

    // Several starts, spread over the plausible box, so the answer does not depend on
    // where the old constants happened to sit.
    const std::vector<Params> starts = {
        compiled,
        { 0.22, 0.10, 1.50, 2.00, 0.35 },
        { 0.25, 0.20, 1.20, 1.50, 0.30 },
        { 0.21, 0.05, 2.00, 3.00, 0.30 },
        { 0.28, 0.00, 1.80, 2.50, 0.38 },
        { 0.20, 0.30, 1.00, 1.00, 0.25 },
    };
    Params best = compiled;
    double bestCost = cost(shots, compiled);
    for (const Params &s0 : starts) {
        Params p = s0;
        for (int round = 0; round < 4; ++round)        // restart from the result
            p = nelderMead(shots, p, 600);
        const double c = cost(shots, p);
        std::printf("start {%.3f %.3f %.3f %.3f %.3f} -> cost %.6f  {%.4f %.4f %.4f %.4f %.4f}\n",
                    s0[0], s0[1], s0[2], s0[3], s0[4], c, p[0], p[1], p[2], p[3], p[4]);
        if (c < bestCost) { bestCost = c; best = p; }
    }
    report("fitted (after)", shots, best);
    return 0;
}
