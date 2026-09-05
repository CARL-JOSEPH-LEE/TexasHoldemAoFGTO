#include "core/SampledSolver.h"
#include "core/Strategy.h"
#include "core/ThreePlayerStrategy.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>

using namespace aof2;
namespace fs = std::filesystem;
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message, double tolerance = 1e-9) {
    require(std::isfinite(actual) && std::abs(actual - expected) < tolerance, message);
}
template<class F> void rejects(F&& f, const char* message) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}

void fixed_rake_tests() {
    const RakeRules fixed{0, 0, true, RakeMode::Fixed, 0.5};
    const std::array<uint16_t, 4> ranks{400, 400, 200, 100};
    MultiwayGame g(4, {}, fixed);
    near(g.settle(3, ranks).rake, .5, "fixed rake on 21.5 BB pot");
    near(g.settle(15, ranks).rake, .5, "fixed rake unchanged on 40 BB pot");
    near(g.settle(15, ranks).ev[0], 39.5 / 2 - 10, "split fixed rake once before payout");
    near(g.settle(1, ranks).rake, 0, "fixed rake respects no flop no drop");
    auto every_pot = fixed; every_pot.no_flop_no_drop = false;
    near(MultiwayGame(4, {}, every_pot).settle(1, ranks).rake, .5, "fixed rake on uncontested pot");
    near(MultiwayGame(4, {}, {.03, .5, false}).settle(1, ranks).rake, .075,
         "fixed rake differs from percentage with cap");
    for (int players : {2, 3, 4}) for (bool nfnd : {false, true}) for (double amount : {0.0, .5, 100.0}) {
        const MultiwayGame small(players, {.05, .1, .2}, {0, 0, nfnd, RakeMode::Fixed, amount});
        for (unsigned mask = 0; mask < (1u << players); ++mask) {
            const auto out = small.settle(mask, ranks);
            const bool showdown = mask && (mask & (mask - 1));
            near(out.rake, nfnd && !showdown ? 0 : std::min(out.pot, amount), "fixed charge follows eligibility and pot limit");
            near(std::accumulate(out.ev.begin(), out.ev.end(), out.rake), 0, "fixed rake conserves chips for every active subset");
        }
    }
    rejects([] { MultiwayGame(4, {}, {0, 0, true, RakeMode::Fixed, -.5}); }, "negative fixed rake rejected");
    rejects([] { MultiwayGame(4, {}, {.03, 0, true, RakeMode::Fixed, .5}); }, "mixed rake modes rejected");
    rejects([] { MultiwayGame(4, {}, {0, 0, true, static_cast<RakeMode>(2), .5}); }, "unknown rake mode rejected");
    rejects([] { MultiwayGame(4, {}, {0, 0, true, RakeMode::Fixed, std::numeric_limits<double>::infinity()}); }, "infinite fixed rake rejected");
}

void rules_tests() {
    std::array<uint16_t, 4> ranks{400, 300, 200, 100};
    for (int n : {2, 3, 4}) {
        MultiwayGame g(n, {}, {0.03, 0, true});
        require(g.nodes.size() == size_t((1 << n) - 2), "decision tree size");
        require(g.node_index[n - 1][0] == -1, "BB walks without a decision");
        auto walk = g.settle(0, ranks);
        near(walk.ev[n - 1], 0.5, "BB walk wins only SB");
        near(walk.ev[n - 2], -0.5, "SB fold loss");
        near(walk.rake, 0, "no flop no drop on walk");
        const auto all = g.settle((1 << n) - 1, ranks);
        near(all.pot, n * 10, "all-in pot includes blinds once");
        near(all.rake, n * 0.3, "3 percent full pot rake");
        near(all.ev[0], n * 9.7 - 10, "all-in winner net payout");
        for (int i = 1; i < n; ++i) near(all.ev[i], -10, "all-in loser contribution");
    }
    MultiwayGame g(4, {}, {0.03, 0, true});
    auto co_btn = g.settle(3, ranks);
    near(co_btn.pot, 21.5, "folded SB and BB remain in pot");
    near(co_btn.rake, 0.645, "rake includes dead blinds");
    near(co_btn.ev[0], 10.855, "CO vs BTN winner with dead blinds");
    auto co_bb = g.settle(9, ranks);
    near(co_bb.pot, 20.5, "SB dead blind counted once");
    ranks = {400, 400, 200, 400};
    auto split = g.settle(15, ranks);
    near(split.ev[0], 38.8 / 3 - 10, "three-way split after single rake");
    near(split.ev[1], split.ev[3], "equal share of split pot");
    near(split.ev[2], -10, "loser gets no tie share");
    MultiwayGame capped(4, {}, {0.03, 0.5, true});
    near(capped.settle(15, ranks).rake, 0.5, "rake cap once per hand");
    MultiwayGame preflop(4, {}, {0.03, 0, false});
    const auto steal = preflop.settle(1, ranks);
    near(steal.pot, 2.5, "uncalled excess refunded before rake");
    near(steal.rake, 0.075, "uncontested matched pot rake");
    near(steal.ev[0], 1.425, "steal net profit");
    near(preflop.settle(0, ranks).rake, 0.03, "walk rake based on matched blind");
    std::mt19937 rng(17);
    for (int n : {2, 3, 4}) for (double rate : {0.0, 0.03, 1.0}) for (double cap : {0.0, 0.1, 100.0}) {
        for (bool nfnd : {false, true}) {
            GameParams params{0.75, 1.5, 12};
            MultiwayGame game(n, params, {rate, cap, nfnd});
            for (unsigned mask = 0; mask < (1u << n); ++mask) for (int sample = 0; sample < 20; ++sample) {
                for (auto& rank : ranks) rank = static_cast<uint16_t>(rng() % 4);
                const auto s = game.settle(mask, ranks);
                near(std::accumulate(s.ev.begin(), s.ev.end(), s.rake), 0, "player EV + house rake conserves chips");
                require(s.rake >= 0 && s.rake <= s.pot, "rake never exceeds pot");
                for (int i = 0; i < n; ++i) require(s.ev[i] >= -params.stack, "loss never exceeds stack");
            }
        }
    }
    rejects([] { MultiwayGame(5); }, "invalid player count rejected");
    rejects([] { MultiwayGame(4, {0.5, 1, 1}); }, "stack must exceed blind");
    rejects([] { MultiwayGame(4, {}, {-0.1, 0, true}); }, "negative rake rejected");
    rejects([] { MultiwayGame(4, {}, {1.01, 0, true}); }, "rake over 100 percent rejected");
    rejects([] { MultiwayGame(4, {}, {0.03, -1, true}); }, "negative cap rejected");
    rejects([] { MultiwayGame(4, {}, {std::numeric_limits<double>::quiet_NaN(), 0, true}); }, "NaN rejected");
}

SampledStrategy uniform(const MultiwayGame& game, double probability) {
    SampledStrategy s;
    s.players = game.players; s.params = game.params; s.rake = game.rake;
    for (const auto& decision : game.nodes) {
        SampledNode n; n.decision = decision; n.frequency.fill(probability); s.nodes.push_back(n);
    }
    return s;
}

void solver_tests() {
    for (int n : {2, 3, 4}) {
        MultiwayGame game(n, {}, {0.03, 0, true});
        SampledSolver solver(game);
        auto folds = uniform(game, 0);
        solver.evaluate(folds, 30000, 42, 2);
        near(folds.ev[n - 1], 0.5, "all fold policy: BB wins");
        near(folds.ev[n - 2], -0.5, "all fold policy: SB loses blind");
        near(folds.expected_rake, 0, "all fold policy: no rake");
        near(folds.ev_std_error[n - 1], 0, "deterministic walk has zero SE");
        const auto& never = folds.nodes.back();
        for (double w : never.reach) near(w, 0, "unreachable history remains unavailable");
        auto calls = uniform(game, 1);
        solver.evaluate(calls, 200000, 42, 2);
        near(calls.expected_rake, n * 0.3, "all call policy: fixed rake", 1e-8);
        const MultiwayGame fixed_game(n, {}, {0, 0, true, RakeMode::Fixed, .5});
        auto fixed_calls = uniform(fixed_game, 1);
        SampledSolver(fixed_game).evaluate(fixed_calls, 10000, 42, 2);
        near(fixed_calls.expected_rake, .5, "uniform evaluator charges fixed amount once, independent of player count");
        near(std::accumulate(fixed_calls.ev.begin(), fixed_calls.ev.end(), fixed_calls.expected_rake), 0,
             "fixed amount evaluation conserves chips", 1e-8);
        rejects([&] { solver.evaluate(fixed_calls, 1000, 42, 1); }, "evaluation rejects a different rake mode");
        near(std::accumulate(calls.ev.begin(), calls.ev.end(), calls.expected_rake), 0, "evaluated conservation");
        for (int p = 0; p < n; ++p)
            require(std::abs(calls.ev[p] + 0.3) < 6 * calls.ev_std_error[p], "uniform random cards are seat symmetric");
        const auto& root = calls.nodes.front();
        for (double w : root.reach) near(w, 1, "root hand conditional reach");
        double count = std::accumulate(root.effective_samples.begin(), root.effective_samples.end(), 0.0);
        near(count, 200000, "each dealt root hand counted exactly once");
        SampledSolver::Config cfg;
        cfg.iterations = 100000; cfg.evaluation_samples = 20000; cfg.seed = 15; cfg.threads = 1; cfg.log_every = 0;
        const auto s1 = solver.solve(cfg);
        cfg.threads = 4;
        const auto s4 = solver.solve(cfg);
        for (size_t node = 0; node < s1.nodes.size(); ++node)
            require(s1.nodes[node].frequency == s4.nodes[node].frequency, "seed reproducible across thread counts");
        require(s1.ev == s4.ev, "evaluation reproducible across thread counts");
        const auto path = fs::temp_directory_path() / ("aof-test-" + std::to_string(n) + ".bin");
        s1.save(path.string());
        SampledStrategy loaded; loaded.load(path.string());
        require(loaded.nodes.back().frequency == s1.nodes.back().frequency, "strategy roundtrip");
        near(loaded.rake.rate, 0.03, "rake metadata roundtrip");
        near(loaded.expected_rake, s1.expected_rake, "evaluation metadata roundtrip");
        std::ofstream extra(path, std::ios::binary | std::ios::app); extra.put('x'); extra.close();
        rejects([&] { loaded.load(path.string()); }, "trailing data rejected");
        s1.save(path.string()); fs::resize_file(path, 31);
        rejects([&] { loaded.load(path.string()); }, "truncated file rejected");
        fs::remove(path);
        cfg.iterations = 0;
        rejects([&] { solver.solve(cfg); }, "zero iterations rejected");
    }
}

int main() {
    try {
        rules_tests(); fixed_rake_tests(); solver_tests();
        std::cout << checks << " core checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
