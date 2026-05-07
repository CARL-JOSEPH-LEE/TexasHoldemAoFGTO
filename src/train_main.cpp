#include "core/CfrSolver.h"
#include "core/EquityTable.h"
#include "core/HandClass.h"
#include "core/PushFoldGame.h"
#include "core/Strategy.h"
#include "core/ThreePlayerCfrSolver.h"
#include "core/ThreePlayerStrategy.h"
#include "core/ThreeWayEquityTable.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Args
{
    std::string equity_path  = "data/equity_table.bin";
    std::string equity3_path = "data/equity_table_3way.bin";
    std::string strategy_path = "data/strategy.bin";
    std::string strategy3_path = "data/strategy_3p.bin";
    uint64_t iterations = 200000;
    unsigned threads    = 0;
    bool     recompute_equity = false;
    aof2::GameParams params;
    uint64_t log_every  = 1000;
    int players = 2;
};

[[noreturn]] void usage_die()
{
    std::cerr <<
        "Usage: aof2_train [options]\n"
        "  --players 2|3        number of players (default 2)\n"
        "  --equity <path>      2-way equity table path (default data/equity_table.bin)\n"
        "  --equity3 <path>     3-way equity table path (default data/equity_table_3way.bin)\n"
        "  --strategy <path>    2-player strategy output path\n"
        "  --strategy3 <path>   3-player strategy output path\n"
        "  --iters <N>          CFR+ iterations (default 200000)\n"
        "  --threads <N>        thread count (0 = hardware concurrency)\n"
        "  --stack <BB>         effective stack in BB (default 10)\n"
        "  --sb-blind <BB>      SB blind in BB (default 0.5)\n"
        "  --bb-blind <BB>      BB blind in BB (default 1)\n"
        "  --recompute-equity   force recompute equity tables\n"
        "  --log-every <N>      progress log every N iterations\n";
    std::exit(2);
}

Args parse(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need = [&](int) -> std::string {
            if (i + 1 >= argc) usage_die();
            return std::string(argv[++i]);
        };
        if      (arg == "--players")    a.players = std::stoi(need(1));
        else if (arg == "--equity")     a.equity_path = need(1);
        else if (arg == "--equity3")    a.equity3_path = need(1);
        else if (arg == "--strategy")   a.strategy_path = need(1);
        else if (arg == "--strategy3")  a.strategy3_path = need(1);
        else if (arg == "--iters")      a.iterations = std::stoull(need(1));
        else if (arg == "--threads")    a.threads = static_cast<unsigned>(std::stoul(need(1)));
        else if (arg == "--stack")      a.params.stack = std::stod(need(1));
        else if (arg == "--sb-blind")   a.params.sb_blind = std::stod(need(1));
        else if (arg == "--bb-blind")   a.params.bb_blind = std::stod(need(1));
        else if (arg == "--recompute-equity") a.recompute_equity = true;
        else if (arg == "--log-every")  a.log_every = std::stoull(need(1));
        else if (arg == "-h" || arg == "--help") usage_die();
        else { std::cerr << "unknown arg: " << arg << "\n"; usage_die(); }
    }
    if (a.players != 2 && a.players != 3) {
        std::cerr << "--players must be 2 or 3 (got " << a.players << ")\n";
        usage_die();
    }
    return a;
}

void print_grid(const std::array<double, aof2::NUM_HAND_CLASSES>& vec, const char* title)
{
    static constexpr char rank_chars[] = "23456789TJQKA";
    std::printf("\n%s grid:\n     ", title);
    for (int c = aof2::NUM_RANKS - 1; c >= 0; --c)
        std::printf(" %c    ", rank_chars[c]);
    std::printf("\n");

    for (int r = aof2::NUM_RANKS - 1; r >= 0; --r) {
        std::printf(" %c |", rank_chars[r]);
        for (int c = aof2::NUM_RANKS - 1; c >= 0; --c) {
            int idx;
            if (r == c) {
                idx = aof2::HandClass(r, r, aof2::HandClass::Kind::Pair).index();
            } else {
                int rh = std::max(r, c), rl = std::min(r, c);
                bool suited = c >= r;
                idx = aof2::HandClass(rh, rl,
                    suited ? aof2::HandClass::Kind::Suited
                           : aof2::HandClass::Kind::Offsuit).index();
            }
            std::printf(" %5.1f", 100.0 * vec[idx]);
        }
        std::printf("\n");
    }
    std::printf("\n");
}

void ensure_dir_for(const std::string& path)
{
    if (path.empty()) return;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
}

void run_2p(const Args& a)
{
    ensure_dir_for(a.strategy_path);
    ensure_dir_for(a.equity_path);

    aof2::EquityTable et;

    if (!a.recompute_equity && fs::exists(a.equity_path)) {
        std::cerr << "[train2p] loading 2-way equity table from " << a.equity_path << "\n";
        et.load(a.equity_path);
    } else {
        std::cerr << "[train2p] computing 2-way equity table (one-time, can take minutes)...\n";
        et.compute(a.threads, true);
        std::cerr << "[train2p] saving 2-way equity table to " << a.equity_path << "\n";
        et.save(a.equity_path);
    }

    aof2::CfrSolver solver(et, a.params);
    aof2::CfrSolver::Config cfg;
    cfg.iterations = a.iterations;
    cfg.threads = a.threads;
    cfg.log_every = a.log_every;
    cfg.on_progress = [](const aof2::CfrProgress& p) {
        std::fprintf(stderr,
            "[cfr+ 2p] iter=%-9llu sb_ev=%+.6f BB  expl=%.6f BB  t=%.2fs\n",
            static_cast<unsigned long long>(p.iteration),
            p.sb_ev, p.exploitability_bb, p.seconds);
    };

    aof2::Strategy s = solver.solve(cfg);
    s.save(a.strategy_path);

    std::cerr << "[train2p] strategy saved to " << a.strategy_path << "\n";
    std::cerr << "[train2p] final SB EV (BB)         = " << s.sb_ev << "\n";
    std::cerr << "[train2p] final exploitability (BB)= " << s.exploitability_bb << "\n";

    print_grid(s.sb_push, "SB push%");
    print_grid(s.bb_call, "BB call%");
}

void run_3p(const Args& a)
{
    ensure_dir_for(a.strategy3_path);
    ensure_dir_for(a.equity_path);
    ensure_dir_for(a.equity3_path);

    aof2::EquityTable et;
    if (!a.recompute_equity && fs::exists(a.equity_path)) {
        std::cerr << "[train3p] loading 2-way equity table from " << a.equity_path << "\n";
        et.load(a.equity_path);
    } else {
        std::cerr << "[train3p] computing 2-way equity table...\n";
        et.compute(a.threads, true);
        std::cerr << "[train3p] saving 2-way equity table to " << a.equity_path << "\n";
        et.save(a.equity_path);
    }

    aof2::ThreeWayEquityTable et3;
    if (!a.recompute_equity && fs::exists(a.equity3_path)) {
        std::cerr << "[train3p] loading 3-way equity table from " << a.equity3_path << "\n";
        et3.load(a.equity3_path);
    } else {
        std::cerr << "[train3p] computing 3-way equity table (NOTE: this can take many hours on a laptop;"
                     " use the 128-core server)...\n";
        et3.compute(a.threads, true);
        std::cerr << "[train3p] saving 3-way equity table to " << a.equity3_path << "\n";
        et3.save(a.equity3_path);
    }

    aof2::ThreePlayerCfrSolver solver(et, et3, a.params);
    aof2::ThreePlayerCfrSolver::Config cfg;
    cfg.iterations = a.iterations;
    cfg.threads = a.threads;
    cfg.log_every = a.log_every;
    cfg.on_progress = [](const aof2::CfrProgress3& p) {
        std::fprintf(stderr,
            "[cfr+ 3p] iter=%-9llu BTN=%+0.5f SB=%+0.5f BB=%+0.5f BB"
            "  expl=%.6f BB  t=%.2fs\n",
            static_cast<unsigned long long>(p.iteration),
            p.btn_ev, p.sb_ev, p.bb_ev, p.exploitability_bb, p.seconds);
    };

    aof2::ThreePlayerStrategy s = solver.solve(cfg);
    s.save(a.strategy3_path);

    std::cerr << "[train3p] strategy saved to " << a.strategy3_path << "\n";
    std::cerr << "[train3p] final BTN EV / SB EV / BB EV (BB) = "
              << s.btn_ev << " / " << s.sb_ev << " / " << s.bb_ev << "\n";
    std::cerr << "[train3p] final exploitability (BB)         = "
              << s.exploitability_bb << "\n";

    print_grid(s.btn_push,            "BTN push%");
    print_grid(s.sb_call_vs_btn_push, "SB call% (vs BTN push)");
    print_grid(s.sb_push_vs_btn_fold, "SB push% (vs BTN fold)");
    print_grid(s.bb_call_3way,        "BB call% (BTN+SB all-in)");
    print_grid(s.bb_call_vs_btn_only, "BB call% (vs BTN only, SB folded)");
    print_grid(s.bb_call_vs_sb_only,  "BB call% (vs SB only, BTN folded)");
}

}

int main(int argc, char** argv)
{
    Args a = parse(argc, argv);
    if (a.players == 2)      run_2p(a);
    else if (a.players == 3) run_3p(a);
    return 0;
}
