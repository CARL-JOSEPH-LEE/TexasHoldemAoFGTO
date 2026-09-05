#include "core/CfrSolver.h"
#include "core/EquityTable.h"
#include "core/HandClass.h"
#include "core/PushFoldGame.h"
#include "core/Strategy.h"
#include "core/ThreePlayerCfrSolver.h"
#include "core/ThreePlayerStrategy.h"
#include "core/ThreeWayEquityTable.h"
#include "core/SampledSolver.h"
#include "core/StratifiedSolver.h"
#include "core/AtomicFile.h"
#include "Utf8Arguments.h"
#include "Version.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;

namespace {

struct Args
{
    std::string equity_path  = "data/equity_table.bin";
    std::string equity3_path = "data/equity_table_3way.bin";
    std::string strategy_path;
    std::string strategy3_path = "data/strategy_3p.bin";
    std::string strategy4_path;
    std::string engine = "stratified";
    std::string weighting = "linear";
    std::string discounting = "none";
    uint64_t schedule_sweeps = 0;
    uint64_t audit_samples = 0;
    unsigned batch_samples = 4;
    double confidence = 0.99;
    std::string checkpoint_path, resume_path;
    uint64_t checkpoint_every = 10000000;
    std::string csv_path;
    aof2::RakeRules rake;
    uint64_t seed = 2026;
    uint64_t eval_samples = 200000;
    uint64_t iterations = 2000000;
    unsigned threads    = 0;
    bool     recompute_equity = false;
    aof2::GameParams params;
    uint64_t log_every  = 100000;
    int players = 2;
};

[[noreturn]] void usage_die(int code = 2)
{
    std::cerr <<
        "Usage: aof2_train [options]\n"
        "  --players 2|3|4      number of players (default 2)\n"
        "  --engine stratified|sampled|table   default stratified; sampled is the v1 baseline\n"
        "  --weighting linear|uniform   signed regret/average weighting (default linear)\n"
        "  --discounting none|dcfr|hs-dcfr  experimental sampled discounting (default none)\n"
        "  --schedule-sweeps <N> fixed HS horizon (default initial training target, retained on resume)\n"
        "  --batch-samples <N>  samples per stratum per sweep, 1..1024 (default 4)\n"
        "  --audit-samples <N>  conditional precision audit budget (default eval-samples)\n"
        "  --confidence <P>     simultaneous audit confidence, 0<P<1 (default .99)\n"
        "  --checkpoint <path> atomically save regrets, average and RNG state\n"
        "  --resume <path>      continue exact training state; --iters is total target budget\n"
        "  --checkpoint-every <N> sample budget between saves (default 10000000)\n"
        "  --rake-percent <P>   pot rake percentage, e.g. 3 = 3% (default 0)\n"
        "  --rake <fraction>    alternative fraction, e.g. 0.03 = 3%\n"
        "  --rake-fixed <BB>    fixed rake per eligible pot; excludes percentage/cap options\n"
        "  --rake-cap <BB>      cap per hand; 0 = unlimited (default 0)\n"
        "  --rake-uncontested   also rake matched pots when everyone folds\n"
        "  --no-flop-no-drop    no rake without showdown (default)\n"
        "  --seed <N>           reproducible sampling seed (default 2026)\n"
        "  --eval-samples <N>   independent evaluation deals (default 200000)\n"
        "  --csv <path>         export sampled strategy with rules and EVs\n"
        "  --equity <path>      2-way equity table path (default data/equity_table.bin)\n"
        "  --equity3 <path>     3-way equity table path (default data/equity_table_3way.bin)\n"
        "  --strategy <path>    strategy output path for any player count\n"
        "  --strategy3 <path>   3-player strategy output path\n"
        "  --strategy4 <path>   alternative 4-player output path\n"
        "  --iters <N>          sample budget (default 2000000); stratified rounds up to full sweeps\n"
        "  --threads <N>        thread count (0 = hardware concurrency)\n"
        "  --stack <BB>         effective stack in BB (default 10)\n"
        "  --sb-blind <BB>      SB blind in BB (default 0.5)\n"
        "  --bb-blind <BB>      BB blind in BB (default 1)\n"
        "  --recompute-equity   force recompute equity tables\n"
        "  --log-every <N>      progress interval; 0 = final only\n";
    std::exit(code);
}

Args parse(int argc, char** argv)
{
    Args a;
    bool iters_set = false, log_set = false, strategy3_set = false;
    bool percentage_set = false, fixed_set = false, cap_set = false;
    auto integer = [](const std::string& s) -> uint64_t {
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("expected a nonnegative integer: " + s);
        return std::stoull(s);
    };
    auto real = [](const std::string& s) {
        size_t used = 0;
        const double value = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(value)) throw std::invalid_argument("invalid number: " + s);
        return value;
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need = [&](int) -> std::string {
            if (i + 1 >= argc) usage_die();
            return std::string(argv[++i]);
        };
        if      (arg == "--players") {
            const auto n = integer(need(1));
            if (n < 2 || n > 4) throw std::invalid_argument("--players must be 2, 3 or 4");
            a.players = static_cast<int>(n);
        }
        else if (arg == "--engine") a.engine = need(1);
        else if (arg == "--discounting") a.discounting = need(1);
        else if (arg == "--schedule-sweeps") a.schedule_sweeps = integer(need(1));
        else if (arg == "--weighting") a.weighting = need(1);
        else if (arg == "--checkpoint") a.checkpoint_path = need(1);
        else if (arg == "--resume") a.resume_path = need(1);
        else if (arg == "--checkpoint-every") {
            a.checkpoint_every = integer(need(1));
            if (!a.checkpoint_every) throw std::invalid_argument("checkpoint-every must be positive");
        }
        else if (arg == "--audit-samples") {
            a.audit_samples = integer(need(1));
            if (a.audit_samples < 2) throw std::invalid_argument("audit-samples must be >= 2");
        }
        else if (arg == "--confidence") a.confidence = real(need(1));
        else if (arg == "--batch-samples") {
            const auto n = integer(need(1));
            if (n < 1 || n > 1024) throw std::invalid_argument("batch-samples must be 1..1024");
            a.batch_samples = static_cast<unsigned>(n);
        }
        else if (arg == "--rake-percent") { a.rake.rate = real(need(1)) / 100.0; percentage_set = true; }
        else if (arg == "--rake") { a.rake.rate = real(need(1)); percentage_set = true; }
        else if (arg == "--rake-cap") { a.rake.cap = real(need(1)); cap_set = true; }
        else if (arg == "--rake-fixed") {
            a.rake.fixed = real(need(1)); a.rake.mode = aof2::RakeMode::Fixed; fixed_set = true;
        }
        else if (arg == "--rake-uncontested") a.rake.no_flop_no_drop = false;
        else if (arg == "--no-flop-no-drop") a.rake.no_flop_no_drop = true;
        else if (arg == "--seed") a.seed = integer(need(1));
        else if (arg == "--eval-samples") a.eval_samples = integer(need(1));
        else if (arg == "--csv") a.csv_path = need(1);
        else if (arg == "--equity")     a.equity_path = need(1);
        else if (arg == "--equity3")    a.equity3_path = need(1);
        else if (arg == "--strategy")   a.strategy_path = need(1);
        else if (arg == "--strategy3")  { a.strategy3_path = need(1); strategy3_set = true; }
        else if (arg == "--strategy4")  a.strategy4_path = need(1);
        else if (arg == "--iters")      { a.iterations = integer(need(1)); iters_set = true; }
        else if (arg == "--threads") {
            const auto n = integer(need(1));
            if (n > 256) throw std::invalid_argument("--threads must be <= 256");
            a.threads = static_cast<unsigned>(n);
        }
        else if (arg == "--stack")      a.params.stack = real(need(1));
        else if (arg == "--sb-blind")   a.params.sb_blind = real(need(1));
        else if (arg == "--bb-blind")   a.params.bb_blind = real(need(1));
        else if (arg == "--recompute-equity") a.recompute_equity = true;
        else if (arg == "--log-every")  { a.log_every = integer(need(1)); log_set = true; }
        else if (arg == "-h" || arg == "--help") usage_die(0);
        else { std::cerr << "unknown arg: " << arg << "\n"; usage_die(); }
    }
    a.params.validate();
    if (fixed_set && (percentage_set || cap_set))
        throw std::invalid_argument("--rake-fixed cannot be combined with --rake-percent, --rake or --rake-cap");
    a.rake.validate();
    if (a.iterations == 0 || a.iterations > 1000000000000ULL || a.eval_samples < 2 || a.eval_samples > 1000000000000ULL)
        throw std::invalid_argument("iters must be 1..10^12; eval-samples must be 2..10^12");
    if (a.engine != "stratified" && a.engine != "sampled" && a.engine != "table")
        throw std::invalid_argument("--engine must be stratified, sampled or table");
    if (a.discounting != "none" && a.discounting != "dcfr" && a.discounting != "hs-dcfr")
        throw std::invalid_argument("invalid discounting algorithm");
    if ((a.discounting != "none" && (a.engine != "stratified" || a.weighting != "linear"))
        || (a.schedule_sweeps && a.discounting != "hs-dcfr") || a.schedule_sweeps > 1000000000000ULL)
        throw std::invalid_argument("discounting requires stratified engine; do not combine with uniform weighting");
    if (a.weighting != "linear" && a.weighting != "uniform") throw std::invalid_argument("invalid weighting");
    if (a.confidence <= 0 || a.confidence >= 1 || a.audit_samples > 1000000000000ULL)
        throw std::invalid_argument("invalid confidence or audit budget");
    if (!a.audit_samples) a.audit_samples = a.eval_samples;
    if (a.engine != "stratified" && (!a.checkpoint_path.empty() || !a.resume_path.empty()))
        throw std::invalid_argument("checkpoints require the stratified engine");
    if (a.engine == "table") {
        if (a.players == 4 || a.rake.rate != 0 || a.rake.cap != 0 || a.rake.is_fixed() || !a.rake.no_flop_no_drop || !a.csv_path.empty())
            throw std::invalid_argument("4P, rake and CSV require --engine stratified or sampled");
        if (!iters_set) a.iterations = 200000;
        if (!log_set) a.log_every = 1000;
        if (a.strategy_path.empty()) a.strategy_path = "data/strategy.bin";
        else if (a.players == 3) a.strategy3_path = a.strategy_path;
    } else {
        if (a.recompute_equity) throw std::invalid_argument("sampled engine does not use equity caches");
        if (a.strategy_path.empty() && a.players == 3 && strategy3_set) a.strategy_path = a.strategy3_path;
        if (a.strategy_path.empty() && a.players == 4) a.strategy_path = a.strategy4_path;
        if (a.strategy_path.empty()) {
            std::ostringstream name;
            name << "data/strategy_" << a.engine << '_' << a.players << "p_" << a.params.stack << "bb_sb" << a.params.sb_blind
                 << "_bb" << a.params.bb_blind;
            if (a.rake.is_fixed()) name << "_rake-fixed" << a.rake.fixed;
            else name << "_rake" << a.rake.rate * 100 << "_cap" << a.rake.cap;
            name << (a.rake.no_flop_no_drop ? "_nfnd.bin" : "_allpots.bin");
            a.strategy_path = name.str();
        }
    }
    if (aof2::output_paths_overlap(a.csv_path, a.strategy_path))
        throw std::invalid_argument("CSV output must differ from the strategy file");
    for (const auto& checkpoint : {a.checkpoint_path, a.resume_path}) if (!checkpoint.empty()) {
        if (aof2::output_paths_overlap(checkpoint, a.strategy_path) || aof2::output_paths_overlap(checkpoint, a.csv_path))
            throw std::invalid_argument("checkpoint must differ from strategy and CSV output");
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
                bool suited = r > c;
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
    fs::path p = fs::u8path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
}

void run_2p(const Args& a)
{
    ensure_dir_for(a.strategy_path);
    ensure_dir_for(a.equity_path);

    aof2::EquityTable et;

    if (!a.recompute_equity && fs::exists(fs::u8path(a.equity_path))) {
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
    if (!a.recompute_equity && fs::exists(fs::u8path(a.equity_path))) {
        std::cerr << "[train3p] loading 2-way equity table from " << a.equity_path << "\n";
        et.load(a.equity_path);
    } else {
        std::cerr << "[train3p] computing 2-way equity table...\n";
        et.compute(a.threads, true);
        std::cerr << "[train3p] saving 2-way equity table to " << a.equity_path << "\n";
        et.save(a.equity_path);
    }

    aof2::ThreeWayEquityTable et3;
    if (!a.recompute_equity && fs::exists(fs::u8path(a.equity3_path))) {
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
    try {
        Utf8Arguments arguments(argc, argv);
        if (version_requested(arguments.count(), arguments.data())) return 0;
        Args a = parse(arguments.count(), arguments.data());
        if (a.engine == "table") {
            if (a.players == 2) run_2p(a); else run_3p(a);
        } else {
            ensure_dir_for(a.strategy_path);
            ensure_dir_for(a.csv_path);
            aof2::MultiwayGame game(a.players, a.params, a.rake);
            std::cerr << "[rules] players=" << a.players << " stack=" << a.params.stack << " rake=";
            if (a.rake.is_fixed()) std::cerr << "fixed " << a.rake.fixed << " BB";
            else std::cerr << a.rake.rate * 100 << "% cap=" << a.rake.cap;
            std::cerr << " no_flop_no_drop=" << a.rake.no_flop_no_drop << " nodes=" << game.nodes.size() << '\n';
            aof2::StratifiedSolver::Config cfg;
            cfg.iterations = a.iterations; cfg.seed = a.seed; cfg.threads = a.threads;
            cfg.evaluation_samples = a.eval_samples; cfg.log_every = a.log_every;
            cfg.audit_samples = a.audit_samples; cfg.confidence = a.confidence;
            cfg.discounting = a.discounting == "dcfr" ? aof2::Discounting::DCFR : a.discounting == "hs-dcfr" ? aof2::Discounting::HS : aof2::Discounting::None;
            cfg.schedule_sweeps = a.schedule_sweeps;
            cfg.samples_per_stratum = a.batch_samples; cfg.linear_weighting = a.weighting == "linear";
            cfg.checkpoint_path = a.checkpoint_path; cfg.resume_path = a.resume_path; cfg.checkpoint_every = a.checkpoint_every;
            cfg.on_progress = [log_every = a.log_every, engine = a.engine](const aof2::SampledProgress& p) {
                if (p.evaluating && !log_every && p.completed != p.total) return;
                std::cerr << (p.auditing ? "[audit] " : p.evaluating ? "[evaluate] "
                    : engine == "stratified" ? "[stratified-cfr] " : "[sampled-cfr] ")
                          << "completed=" << p.completed << " total=" << p.total << " seconds=" << p.seconds << '\n';
            };
            auto result = a.engine == "stratified" ? aof2::StratifiedSolver(game).solve(cfg)
                                                  : aof2::SampledSolver(game).solve(cfg);
            result.save(a.strategy_path);
            if (!a.csv_path.empty()) result.export_csv(a.csv_path);
            std::cout << "Strategy saved: " << a.strategy_path << '\n';
            for (int p = 0; p < a.players; ++p)
                std::cout << game.position(p) << " EV=" << result.ev[p] << " BB (SE " << result.ev_std_error[p] << ")\n";
            std::cout << "Expected rake=" << result.expected_rake << " BB/hand\n"
                      << "Sampled deviation sum=" << result.sampled_nash_conv << " BB/hand\n"
                      << "Sampling diagnostic only; an estimated gap is not an exact Nash certificate.\n";
            if (result.audit_samples)
                std::cout << "Simultaneous confidence=" << result.confidence << " deviation upper=" << result.deviation_upper
                          << " BB/hand; conditional samples=" << result.audit_samples << "; sweeps=" << result.training_sweeps << '\n';
            if (result.audit_samples) for (int p = 0; p < a.players; ++p)
                std::cout << "Conditional " << game.position(p) << " EV=" << result.conditional_ev[p]
                          << " BB (SE " << result.conditional_ev_std_error[p] << ")\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }
}
