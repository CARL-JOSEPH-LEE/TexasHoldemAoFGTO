#include "core/StratifiedSolver.h"
#include "core/RangeSampler.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>

using namespace aof2;
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void near(double a, double b, double tolerance, const char* message) {
    check(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
SampledStrategy policy(const MultiwayGame& g, double f) {
    SampledStrategy s; s.players = g.players; s.params = g.params; s.rake = g.rake;
    for (auto d : g.nodes) { SampledNode n; n.decision = d; n.frequency.fill(f); s.nodes.push_back(n); }
    return s;
}
void sampling_tests() {
    HandValues values{};
    for (int h = 0; h < 169; ++h) values[h] = (h % 11) / 10.0;
    RangeSampler sampler(values);
    std::mt19937_64 rng(7123);
    for (int repetition = 0; repetition < 200; ++repetition) {
        std::array<uint8_t, 8> blocked{};
        const int count = 2 + 2 * (repetition % 3);
        uint64_t bits = 0;
        for (int i = 0; i < count; ++i) {
            uint8_t c;
            do { c = std::uniform_int_distribution<int>(0, 51)(rng); } while (bits & (uint64_t{1} << c));
            bits |= uint64_t{1} << c; blocked[i] = c;
        }
        double exact = 0;
        for (int a = 0; a < 52; ++a) for (int b = 0; b < a; ++b) {
            if (!(bits & ((uint64_t{1} << a) | (uint64_t{1} << b))))
                exact += values[HandClass::from_combo(a, b).index()];
        }
        const double mass = sampler.available_mass(blocked, count);
        near(mass, exact, 1e-9, "legal mass equals independent physical-combo enumeration");
        for (int n = 0; n < 50; ++n) {
            const auto c = sampler.sample(bits, mass, rng);
            check(!(RangeSampler::bits(c) & bits), "range sampler never reuses a card");
            check(sampler.weight(c) > 0, "zero-weight hands never drawn");
        }
    }
    // Exact collision conditioning with almost all proposal mass blocked.
    values.fill(0); values[12] = 1; values[11] = 1e-15;
    sampler.reset(values, true);
    std::array<uint8_t, 8> aces{48,49,50,51};
    const double mass = sampler.available_mass(aces, 4);
    near(mass, 6e-15, 1e-27, "tiny legal mass retained without cancellation");
    const auto kk = sampler.sample((uint64_t{15} << 48), mass, rng);
    check(HandClass::from_combo(kk.card_high, kk.card_low).index() == 11, "rare-mass fallback samples legal KK");

    // Compare a TWO-opponent importance estimate against exhaustive enumeration.
    // This detects missing blocker normalizers even when every hand is legal.
    HandValues a{}, b{};
    a[12] = .8; a[11] = .3; a[10] = .6; a[90] = .7;
    b[12] = .2; b[11] = .9; b[9] = .5; b[90] = .4;
    RangeSampler r1(a), r2(b);
    const auto hero = RangeSampler::combinations()[12].front();
    const uint64_t hero_bits = RangeSampler::bits(hero);
    double exact = 0;
    for (int h1 = 0; h1 < 169; ++h1) if (a[h1]) for (auto c1 : RangeSampler::combinations()[h1]) {
        if (RangeSampler::bits(c1) & hero_bits) continue;
        for (int h2 = 0; h2 < 169; ++h2) if (b[h2]) for (auto c2 : RangeSampler::combinations()[h2]) {
            if (RangeSampler::bits(c2) & (hero_bits | RangeSampler::bits(c1))) continue;
            exact += a[h1] * b[h2] * (h1 > h2 ? 3 : -2) / (1225.0 * 1128.0);
        }
    }
    double sum = 0, squared = 0;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        std::array<uint8_t, 8> blocked{hero.card_high, hero.card_low};
        const double m1 = r1.available_mass(blocked, 2);
        const auto c1 = r1.sample(hero_bits, m1, rng);
        blocked[2] = c1.card_high; blocked[3] = c1.card_low;
        const double m2 = r2.available_mass(blocked, 4);
        const auto c2 = r2.sample(hero_bits | RangeSampler::bits(c1), m2, rng);
        const double value = m1 / 1225 * m2 / 1128 *
            (HandClass::from_combo(c1.card_high,c1.card_low).index() > HandClass::from_combo(c2.card_high,c2.card_low).index() ? 3 : -2);
        sum += value; squared += value * value;
    }
    const double se = std::sqrt((squared - sum * sum / n) / (n - 1) / n);
    near(sum / n, exact, 6 * se, "importance expectation agrees with exact blocker-aware enumeration");
}
void evaluation_tests() {
    for (int n : {2,3,4}) {
        MultiwayGame g(n, {}, {.03, .5, true});
        StratifiedSolver solver(g);
        auto s = policy(g, 0);
        solver.evaluate(s, 10000, 30000, 11, 2);
        for (int p = 0; p < n; ++p) near(s.ev[p], s.conditional_ev[p], 1e-8, "all-fold evaluators agree exactly");
        near(s.nodes[0].action_ev[12], n == 2 ? 1 : 1.5, 1e-8, "uncontested action EV is analytic");
        for (double x : s.nodes.back().effective_samples) near(x, 0, 1e-8, "impossible ranges stay unreachable");
        // Non-uniform mixed policies, independent sampling algorithms/seeds.
        for (size_t k = 0; k < s.nodes.size(); ++k) for (int h = 0; h < 169; ++h)
            s.nodes[k].frequency[h] = .05 + .9 * ((h * 17 + k * 29) % 101) / 100.0;
        solver.evaluate(s, 500000, 1000000, 8282, 4);
        for (int p = 0; p < n; ++p)
            near(s.ev[p], s.conditional_ev[p], 7 * std::hypot(s.ev_std_error[p], s.conditional_ev_std_error[p]),
                 "uniform and conditional estimators agree within sampling error");
        check(s.deviation_upper >= s.sampled_nash_conv, "simultaneous bound dominates point estimate");
        for (auto& node : s.nodes) for (int h = 0; h < 169; ++h) {
            near(node.fold_ev[h], -g.blind(node.decision.seat), 0, "fold EV is exact");
            const double d = node.action_ev[h] - node.fold_ev[h];
            check(node.advantage_lower[h] <= d && d <= node.advantage_upper[h], "conditional EV interval contains estimate");
        }
    }
    // Only KK opens: AA at BB sees exact history probability 6/C(50,2).
    MultiwayGame g(2); auto s = policy(g, .5); s.nodes[0].frequency.fill(0); s.nodes[0].frequency[11] = 1;
    StratifiedSolver(g).evaluate(s, 10000, 500000, 33, 4);
    near(s.nodes[1].reach[12], 6.0 / 1225, 1e-12, "narrow range posterior normalizer exact");
    // Well-known AA/KK equity lies between .81 and .83 (suits averaged).
    check(s.nodes[1].advantage_lower[12] <= 20 * .82 - 9 && s.nodes[1].advantage_upper[12] >= 20 * .82 - 9,
          "exact reference matchup is covered by uncertainty interval");
    check(s.nodes[1].effective_samples[12] > 900, "rare history gets allocated samples instead of waiting for it");
}
void training_tests() {
    MultiwayGame g(4, {}, {.03, 0, true}); StratifiedSolver solver(g);
    StratifiedSolver::Config c; c.iterations = 100000; c.evaluation_samples = 10000; c.audit_samples = 10000;
    c.seed = 321; c.threads = 1;
    const auto one = solver.solve(c); c.threads = 4; const auto four = solver.solve(c);
    check(one.iterations >= c.iterations, "sample budget rounds up to cover every stratum");
    check(one.iterations - c.iterations < solver.strata() * c.samples_per_stratum, "at most one extra sweep");
    for (size_t k = 0; k < one.nodes.size(); ++k) {
        check(one.nodes[k].frequency == four.nodes[k].frequency, "training reproducible across workers");
        check(one.nodes[k].action_ev == four.nodes[k].action_ev, "conditional audit reproducible across workers");
    }
    const auto file = std::filesystem::temp_directory_path() / "aof-stratified-test.bin";
    one.save(file.string()); SampledStrategy restored; restored.load(file.string()); std::filesystem::remove(file);
    check(restored.training_method == 2 && restored.confidence == .99, "v2 algorithm and confidence metadata roundtrip");
    check(restored.nodes.back().advantage_upper == one.nodes.back().advantage_upper, "precision arrays roundtrip");
    const auto state = std::filesystem::temp_directory_path() / "aof-resume-test.checkpoint";
    c.checkpoint_path = state.string();
    c.iterations = 30000; solver.solve(c);
    c.iterations = 100000; c.resume_path = state.string();
    const auto resumed = solver.solve(c);
    for (size_t k = 0; k < one.nodes.size(); ++k)
        check(resumed.nodes[k].frequency == one.nodes[k].frequency, "resume preserves exact regrets, average and RNG trajectory");
    check(resumed.ev == one.ev && resumed.deviation_upper == one.deviation_upper, "resumed evaluations are bit-identical");
    c.seed += 1;
    bool rejected = false;
    try { solver.solve(c); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "mismatched resume configuration rejected");
    std::filesystem::remove(state);
    c.seed -= 1; c.checkpoint_path.clear(); c.resume_path.clear();
    c.linear_weighting = false;
    check(solver.solve(c).nodes[0].frequency != one.nodes[0].frequency, "linear weighting is active");
    // Analytically solved games: with 100% rake, calling always loses the whole
    // stack. NFND permits profitable steals; raking all pots makes stealing
    // worse than folding too. This checks learning, not just finite outputs.
    c.iterations = 1000000; c.linear_weighting = true;
    for (bool nfnd : {false, true}) {
        MultiwayGame extreme(4, {}, {1, 0, nfnd});
        const auto answer = StratifiedSolver(extreme).solve(c);
        for (const auto& node : answer.nodes) for (double f : node.frequency) {
            if (nfnd && !node.decision.prior_mask) check(f > .95, "learns to steal all hands in exactly solved NFND game");
            else check(f < .05, "learns to fold unprofitable actions in exactly solved game");
        }
    }
}
int main() {
    try { sampling_tests(); evaluation_tests(); training_tests(); std::cout << checks << " stratified checks passed\n"; }
    catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
