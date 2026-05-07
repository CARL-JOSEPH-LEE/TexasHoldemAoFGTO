#include "core/HandClass.h"
#include "core/ThreePlayerPayoff.h"

#include <omp/EquityCalculator.h>
#include <omp/CardRange.h>

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static double calc(const std::string& a, const std::string& b)
{
    omp::EquityCalculator eq;
    std::vector<omp::CardRange> ranges{omp::CardRange(a), omp::CardRange(b)};
    if (!eq.start(ranges, 0, 0, true, 0.0, nullptr, 0.2, 0))
        throw std::runtime_error("start failed: " + a + " vs " + b);
    eq.wait();
    auto r = eq.getResults();
    if (!r.finished || r.hands == 0)
        throw std::runtime_error("incomplete result: " + a + " vs " + b);
    return r.equity[0];
}

int main()
{
    using namespace aof2;

    auto check = [](const HandClass& h, int expect) {
        if (h.index() != expect)
            throw std::logic_error("idx mismatch: " + h.to_string());
    };
    check(HandClass(12, 12, HandClass::Kind::Pair),  12);
    check(HandClass(0, 0,   HandClass::Kind::Pair),  0);
    check(HandClass(12, 11, HandClass::Kind::Suited), 13 + 12*11/2 + 11);
    check(HandClass(12, 11, HandClass::Kind::Offsuit), 91 + 12*11/2 + 11);

    for (int idx = 0; idx < NUM_HAND_CLASSES; ++idx) {
        HandClass h = HandClass::from_index(idx);
        if (h.index() != idx) {
            std::fprintf(stderr, "round-trip fail idx=%d -> %s -> %d\n",
                idx, h.to_string().c_str(), h.index());
            return 2;
        }
    }
    std::fprintf(stderr, "[smoke] hand class round-trip OK (169 indices)\n");

    const double aa_kk_total = 50371344.0 + 10986372.0 + 285228.0;
    const double aa_kk_eq   = (50371344.0 + 285228.0 / 2.0) / aa_kk_total;

    struct Cell { const char* a; const char* b; double expect; double tol; };
    std::vector<Cell> cells = {
        {"AA",  "KK",  aa_kk_eq, 1e-6 },
    };

    auto t0 = std::chrono::steady_clock::now();
    for (auto& c : cells) {
        double e = calc(c.a, c.b);
        double diff = std::abs(e - c.expect);
        std::fprintf(stderr, "[smoke] %4s vs %4s  equity=%.4f  expect~%.4f  diff=%.4f  %s\n",
            c.a, c.b, e, c.expect, diff, (diff < c.tol ? "OK" : "FAIL"));
        if (diff >= c.tol) return 3;
    }

    GameParams p;
    p.stack = 10.0;
    p.sb_blind = 0.5;
    p.bb_blind = 1.0;
    const double sb_good = sb_vs_bb_called_ev(p, 0.80);
    const double sb_bad = sb_vs_bb_called_ev(p, 0.20);
    if (std::abs(sb_good - 6.0) > 1e-12 || std::abs(sb_bad + 6.0) > 1e-12 || !(sb_good > sb_bad)) {
        std::fprintf(stderr, "[smoke] SB-vs-BB payoff direction failed: good=%.6f bad=%.6f\n", sb_good, sb_bad);
        return 4;
    }

    p.stack = 20.0;
    p.bb_blind = 2.0;
    if (std::abs(pot_btn_sb(p) - 42.0) > 1e-12) {
        std::fprintf(stderr, "[smoke] BTN-vs-SB pot failed: pot=%.6f\n", pot_btn_sb(p));
        return 5;
    }

    auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[smoke] %zu cells in %.2fs (avg %.3fs/cell, multi-threaded enum)\n",
        cells.size(), secs, secs / cells.size());

    return 0;
}
