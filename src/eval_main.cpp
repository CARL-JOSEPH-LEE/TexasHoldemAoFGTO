#include "core/CfrSolver.h"
#include "core/EquityTable.h"
#include "core/HandClass.h"
#include "core/Strategy.h"
#include "core/SampledSolver.h"
#include "core/StratifiedSolver.h"
#include "core/AtomicFile.h"
#include "Utf8Arguments.h"
#include "Version.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <iomanip>

int run_eval(int argc, char** argv)
{
    std::string equity_path  = "data/equity_table.bin";
    std::string strategy_path = "data/strategy.bin";
    std::string csv_path;
    uint64_t samples = 1000000, seed = 98765;
    unsigned threads = 0;
    uint64_t audit_samples = 0;
    std::string method = "stratified";
    std::string hand_name;
    int focus_node = -1;
    auto integer = [](const std::string& s) {
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("expected a nonnegative integer");
        return std::stoull(s);
    };

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--equity"   && i + 1 < argc) equity_path  = argv[++i];
        else if (a == "--strategy" && i + 1 < argc) strategy_path = argv[++i];
        else if (a == "--samples" && i + 1 < argc) samples = integer(argv[++i]);
        else if (a == "--audit-samples" && i + 1 < argc) {
            audit_samples = integer(argv[++i]);
            if (audit_samples < 2) throw std::invalid_argument("audit-samples must be >= 2");
        }
        else if (a == "--method" && i + 1 < argc) method = argv[++i];
        else if (a == "--hand" && i + 1 < argc) hand_name = argv[++i];
        else if (a == "--node" && i + 1 < argc) {
            const auto n = integer(argv[++i]);
            if (n > 13) throw std::invalid_argument("node must be 0..13");
            focus_node = static_cast<int>(n);
        }
        else if (a == "--seed" && i + 1 < argc) seed = integer(argv[++i]);
        else if (a == "--csv" && i + 1 < argc) csv_path = argv[++i];
        else if (a == "--threads" && i + 1 < argc) {
            auto n = integer(argv[++i]);
            if (n > 256) throw std::invalid_argument("threads must be <= 256");
            threads = static_cast<unsigned>(n);
        }
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: aof2_eval --strategy <path> [--samples N] [--seed N] [--threads N] [--csv path]\n"
                         "[--method stratified|uniform] [--audit-samples N] (default stratified, audit budget=samples)\n"
                         "[--hand AKs --node 0] fresh focused audit; --samples budget; prints JSON\n"
                         "Sampled 2P/3P/4P files carry all rules; no equity cache is needed.\n"
                         "Legacy 2P files require --equity <path>.\n";
            return 0;
        }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    if (samples < 2 || samples > 1000000000000ULL) throw std::invalid_argument("samples must be 2..10^12");
    if (audit_samples > 1000000000000ULL || (method != "stratified" && method != "uniform"))
        throw std::invalid_argument("invalid audit budget or evaluation method");
    if (!audit_samples) audit_samples = samples;
    if (hand_name.empty() != (focus_node < 0) || (!hand_name.empty() && (!csv_path.empty() || method != "stratified")))
        throw std::invalid_argument("focused audit requires both --hand and --node, stratified method, no CSV");
    if (aof2::output_paths_overlap(csv_path, strategy_path))
        throw std::invalid_argument("CSV output must differ from the strategy file");
    std::ifstream input(std::filesystem::u8path(strategy_path), std::ios::binary);
    char magic[8]{}; input.read(magic, 8);
    if (std::string(magic, 8) == "AOFMSTR1") {
        aof2::SampledStrategy s; s.load(strategy_path);
        aof2::MultiwayGame game(s.players, s.params, s.rake);
        if (!hand_name.empty()) {
            int h = -1;
            for (int i = 0; i < aof2::NUM_HAND_CLASSES; ++i)
                if (aof2::HandClass::from_index(i).to_string() == hand_name) h = i;
            if (h < 0) throw std::invalid_argument("invalid hand; use AA, AKs or AKo");
            const auto a = aof2::StratifiedSolver(game).evaluate_hand(s, focus_node, h, samples, seed, threads);
            std::cout << std::setprecision(17) << "{\"schema\":1,\"node\":" << focus_node << ",\"hand\":\"" << hand_name
                << "\",\"samples\":" << a.samples << ",\"seed\":" << a.seed << ",\"confidence\":" << a.confidence
                << ",\"reach\":" << a.reach << ",\"effective_samples\":" << a.effective_samples
                << ",\"action_ev\":" << a.action_ev << ",\"fold_ev\":" << a.fold_ev
                << ",\"std_error\":" << a.std_error << ",\"lower\":" << a.lower << ",\"upper\":" << a.upper << "}\n";
            return 0;
        }
        if (method == "stratified") aof2::StratifiedSolver(game).evaluate(s, samples, audit_samples, seed, threads);
        else aof2::SampledSolver(game).evaluate(s, samples, seed, threads);
        std::cout << "Players=" << s.players << " stack=" << s.params.stack << " BB rake=";
        if (s.rake.is_fixed()) std::cout << "fixed " << s.rake.fixed << " BB";
        else std::cout << s.rake.rate * 100 << "% cap=" << s.rake.cap;
        std::cout << " no_flop_no_drop=" << s.rake.no_flop_no_drop << '\n';
        for (int p = 0; p < s.players; ++p)
            std::cout << game.position(p) << " EV=" << s.ev[p] << " BB/hand; SE=" << s.ev_std_error[p] << '\n';
        std::cout << "Expected rake=" << s.expected_rake << " BB/hand\nSampled deviation sum="
                  << s.sampled_nash_conv << " BB/hand (sampling diagnostic, not a convergence certificate)\n";
        if (s.audit_samples)
            std::cout << "Simultaneous confidence=" << s.confidence << " deviation upper=" << s.deviation_upper
                      << " BB/hand; conditional samples=" << s.audit_samples << '\n';
        if (s.audit_samples) for (int p = 0; p < s.players; ++p)
            std::cout << "Conditional " << game.position(p) << " EV=" << s.conditional_ev[p]
                      << " BB (SE " << s.conditional_ev_std_error[p] << ")\n";
        if (!csv_path.empty()) {
            const auto parent = std::filesystem::u8path(csv_path).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            s.export_csv(csv_path);
        }
        return 0;
    }
    if (!csv_path.empty()) throw std::invalid_argument("CSV evaluation requires a sampled strategy");

    aof2::EquityTable et;
    et.load(equity_path);

    aof2::Strategy s;
    s.load(strategy_path);

    aof2::CfrSolver solver(et, s.params);
    auto bd = solver.breakdown(s.sb_push, s.bb_call);

    auto classes = aof2::all_hand_classes();

    std::printf("                            SB                       |              BB\n");
    std::printf("hand   push%%   push EV     fold EV    diff   |  call%%   call EV     fold EV    diff\n");
    std::printf("-----  ------  ----------  ----------  ------  |  ------  ----------  ----------  ------\n");

    for (int idx = 0; idx < aof2::NUM_HAND_CLASSES; ++idx) {
        const auto h = classes[idx];
        const double pp = s.sb_push[idx];
        const double cp = s.bb_call[idx];
        const double pe = bd.sb_push_ev[idx];
        const double pf = bd.sb_fold_ev;
        const double ce = bd.bb_call_ev[idx];
        const double cf = bd.bb_fold_ev;
        std::printf("%-5s  %5.1f%%  %+9.5f  %+9.5f  %+6.4f  |  %5.1f%%  %+9.5f  %+9.5f  %+6.4f\n",
            h.to_string().c_str(),
            100.0 * pp, pe, pf, pe - pf,
            100.0 * cp, ce, cf, ce - cf);
    }
    return 0;
}

int main(int argc, char** argv) {
    try { Utf8Arguments args(argc, argv); if (version_requested(args.count(), args.data())) return 0; return run_eval(args.count(), args.data()); }
    catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 2; }
}
