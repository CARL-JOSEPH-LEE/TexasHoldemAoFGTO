#include "StratifiedSolver.h"
#include "RangeSampler.h"
#include "ThreadPool.h"
#include "AtomicFile.h"
#include <omp/HandEvaluator.h>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <fstream>
#include <iomanip>
#include <locale>

namespace aof2 {
namespace {
constexpr unsigned LANES = 32;
using Policy = std::vector<HandValues>;
using Clock = std::chrono::steady_clock;
uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
unsigned workers(unsigned requested) {
    if (requested > 256) throw std::invalid_argument("threads must be <= 256");
    return std::max(1u, std::min(LANES, requested ? requested : std::thread::hardware_concurrency()));
}
struct Leaf {
    unsigned mask = 0;
    std::array<int, 4> ranges{};
    double base = 0, payout = 0, lower = 0, upper = 0;
    bool showdown = false;
};
struct Observation { double advantage = 0, weight = 0; };
struct Moments {
    double x = 0, x2 = 0, w = 0, w2 = 0, xw = 0;
    void add(Observation o) {
        x += o.advantage; x2 += o.advantage * o.advantage;
        w += o.weight; w2 += o.weight * o.weight; xw += o.advantage * o.weight;
    }
};
double variance(double sum, double squared, uint64_t n) {
    return std::max(0.0, (squared - sum * sum / n) / (n - 1));
}
// Two-sided empirical Bernstein (Maurer & Pontil, 2009, Theorem 4),
// with a union bound over both moments, both tails and all strata.
double radius(double sum, double squared, uint64_t n, double width, double log_term) {
    return std::sqrt(2 * variance(sum, squared, n) * log_term / n)
        + 7 * width * log_term / (3 * (n - 1));
}

struct Workspace {
    const MultiwayGame& game;
    omp::HandEvaluator evaluator; // initialize the evaluator before workers
    ThreadPool pool;
    std::array<std::mt19937_64, LANES> rng;
    std::vector<RangeSampler> ranges;
    std::vector<std::vector<Leaf>> leaves;
    Workspace(const MultiwayGame& g, uint64_t seed, unsigned threads)
        : game(g), pool(workers(threads)), ranges(2 * g.nodes.size()), leaves(g.nodes.size()) {
        (void)RangeSampler::combinations();
        for (unsigned i = 0; i < LANES; ++i) rng[i].seed(mix(seed + i));
        for (size_t node = 0; node < g.nodes.size(); ++node) {
            const auto d = g.nodes[node];
            for (unsigned future = 0; future < (1u << (g.players - d.seat - 1)); ++future) {
                Leaf leaf;
                leaf.mask = d.prior_mask | (1u << d.seat) | (future << (d.seat + 1));
                for (int p = 0; p < g.players; ++p) if (p != d.seat) {
                    const auto history = leaf.mask & ((1u << p) - 1);
                    leaf.ranges[p] = 2 * g.node_index[p][history] + ((leaf.mask >> p) & 1u);
                }
                leaf.showdown = (leaf.mask & (leaf.mask - 1)) != 0;
                const auto settled = g.settle(leaf.mask, {});
                leaf.payout = settled.pot - settled.rake;
                leaf.base = leaf.showdown ? -g.params.stack + g.blind(d.seat)
                    : settled.ev[d.seat] + g.blind(d.seat);
                leaf.lower = leaf.base;
                leaf.upper = leaf.base + (leaf.showdown ? leaf.payout : 0);
                leaves[node].push_back(leaf);
            }
        }
    }
    void set_policy(const Policy& policy) {
        for (size_t node = 0; node < policy.size(); ++node) {
            ranges[2 * node].reset(policy[node], false);
            ranges[2 * node + 1].reset(policy[node], true);
        }
    }
    Observation sample(const Leaf& leaf, int seat, int hand, std::mt19937_64& random) const {
        std::array<Combo, 4> holes{};
        // Suit-permutation symmetry makes every physical combo in this class
        // equivalent, since ALL policies depend only on the 169 hand classes.
        holes[seat] = RangeSampler::combinations()[hand].front();
        std::array<uint8_t, 8> blocked{holes[seat].card_high, holes[seat].card_low};
        int count = 2;
        uint64_t used = RangeSampler::bits(holes[seat]);
        double weight = 1;
        for (int p = 0; p < game.players; ++p) if (p != seat) {
            const auto& range = ranges[leaf.ranges[p]];
            const double mass = range.available_mass(blocked, count);
            if (mass <= 0) return {};
            // Target chance probability is 1/C(remaining,2). The proposal is
            // action_probability(combo)/legal_range_mass. The action factors
            // cancel, leaving this likelihood correction. Do NOT normalize by
            // sampled history reach in a regret update.
            const double choices = (52 - count) * (51 - count) / 2.0;
            weight *= mass / choices;
            holes[p] = range.sample(used, mass, random);
            used |= RangeSampler::bits(holes[p]);
            blocked[count++] = holes[p].card_high;
            blocked[count++] = holes[p].card_low;
        }
        double advantage = leaf.base;
        if (leaf.showdown) {
            omp::Hand board = omp::Hand::empty();
            for (int i = 0; i < 5; ++i) {
                uint8_t card;
                do { card = static_cast<uint8_t>(std::uniform_int_distribution<int>(0, 51)(random)); }
                while (used & (uint64_t{1} << card));
                used |= uint64_t{1} << card;
                board += omp::Hand(card);
            }
            const auto hero = evaluator.evaluate(board + omp::Hand(holes[seat].card_high) + omp::Hand(holes[seat].card_low));
            unsigned ties = 1;
            bool win = true;
            for (int p = 0; p < game.players; ++p) if (p != seat && (leaf.mask & (1u << p))) {
                const auto rank = evaluator.evaluate(board + omp::Hand(holes[p].card_high) + omp::Hand(holes[p].card_low));
                if (rank > hero) { win = false; break; }
                if (rank == hero) ++ties;
            }
            if (win) advantage += leaf.payout / ties;
        }
        return {weight * advantage, weight};
    }
    double weight_bound(const Leaf& leaf, int seat, int hand) const {
        const auto hero = RangeSampler::combinations()[hand].front();
        std::array<uint8_t, 8> blocked{hero.card_high, hero.card_low};
        double bound = 1;
        int count = 2;
        for (int p = 0; p < game.players; ++p) if (p != seat) {
            // Further blockers can only DECREASE this legal mass.
            const double mass = ranges[leaf.ranges[p]].available_mass(blocked, 2);
            bound *= std::min(1.0, mass / ((52 - count) * (51 - count) / 2.0));
            count += 2;
        }
        return bound;
    }
};

void checkpoint(const std::string& name, const MultiwayGame& game, const StratifiedSolver::Config& cfg,
                uint64_t step, double average_weight, const Policy& policy, const Policy& a, const Policy& b,
                const Policy& average, const Workspace& work) {
    const auto path = std::filesystem::u8path(name);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    out.imbue(std::locale::classic()); out << std::setprecision(17);
    out << "AOFCHK4\n" << game.players << ' ' << game.params.sb_blind << ' ' << game.params.bb_blind << ' '
        << game.params.stack << ' ' << game.rake.rate << ' ' << game.rake.cap << ' ' << game.rake.no_flop_no_drop
        << ' ' << static_cast<unsigned>(game.rake.mode) << ' ' << game.rake.fixed << '\n'
        << cfg.seed << ' ' << cfg.linear_weighting << ' ' << cfg.samples_per_stratum << ' ' << LANES << ' '
        << step << ' ' << average_weight << ' ' << game.nodes.size() << '\n'
        << static_cast<unsigned>(cfg.discounting) << ' ' << cfg.schedule_sweeps << '\n';
    for (const auto* matrix : {&policy, &a, &b, &average}) {
        for (const auto& row : *matrix) { for (double x : row) out << x << ' '; out << '\n'; }
    }
    for (const auto& rng : work.rng) out << rng << '\n';
    out.close();
    if (!out) throw std::runtime_error("checkpoint write failed");
    atomic_replace(temporary, path);
}
uint64_t restore(const std::string& name, const MultiwayGame& game, StratifiedSolver::Config& cfg,
                 double& average_weight, Policy& policy, Policy& a, Policy& b, Policy& average, Workspace& work) {
    const auto path = std::filesystem::u8path(name);
    if (std::filesystem::file_size(path) > 8000000) throw std::invalid_argument("checkpoint too large");
    std::ifstream in(path); in.imbue(std::locale::classic());
    std::string magic; std::getline(in, magic);
    if (magic != "AOFCHK2" && magic != "AOFCHK3" && magic != "AOFCHK4") throw std::invalid_argument("invalid checkpoint format");
    int players = 0, nfnd = 0, linear = 0; GameParams p; RakeRules r;
    uint64_t seed = 0, step = 0, rows = 0; unsigned batch = 0, lanes = 0;
    in >> players >> p.sb_blind >> p.bb_blind >> p.stack >> r.rate >> r.cap >> nfnd;
    unsigned mode = 0;
    if (magic == "AOFCHK4") in >> mode >> r.fixed;
    r.mode = static_cast<RakeMode>(mode);
    r.no_flop_no_drop = nfnd != 0;
    r.validate();
    in >> seed >> linear >> batch >> lanes >> step >> average_weight >> rows;
    unsigned discount = 0; uint64_t horizon = 0;
    if (magic != "AOFCHK2") in >> discount >> horizon;
    if (discount != static_cast<unsigned>(cfg.discounting) || horizon > 1000000000000ULL
        || (discount == 2 && !horizon) || (discount != 2 && horizon)
        || (cfg.schedule_sweeps && cfg.schedule_sweeps != horizon))
        throw std::invalid_argument("checkpoint algorithm or fixed schedule horizon differs");
    cfg.schedule_sweeps = horizon;
    if (!in || players != game.players || p.sb_blind != game.params.sb_blind || p.bb_blind != game.params.bb_blind
        || p.stack != game.params.stack || !(r == game.rake)
        || nfnd != game.rake.no_flop_no_drop || seed != cfg.seed || linear != cfg.linear_weighting
        || batch != cfg.samples_per_stratum || lanes != LANES || rows != game.nodes.size() || step > 1000000000000ULL)
        throw std::invalid_argument("checkpoint rules, seed, weighting or batch size differ from requested training");
    const double expected_weight = cfg.linear_weighting ? double(step) * (double(step) + 1) / 2 : double(step);
    if (!std::isfinite(average_weight) || average_weight < 0
        || (cfg.discounting == Discounting::None && std::abs(average_weight - expected_weight) > 1e-10 * std::max(1.0, expected_weight))
        || (cfg.discounting != Discounting::None && (average_weight < (step ? 1 : 0) || average_weight > step + 1)))
        throw std::invalid_argument("invalid checkpoint averaging state");
    for (auto* matrix : {&policy, &a, &b, &average}) for (auto& row : *matrix) for (double& x : row) {
        in >> x;
        if (!in || !std::isfinite(x) || (matrix == &policy && (x < 0 || x > 1))
            || (matrix == &average && (x < 0 || x > average_weight * 1.000000001)))
            throw std::invalid_argument("invalid checkpoint numeric state");
    }
    for (auto& rng : work.rng) in >> rng;
    if (!in) throw std::invalid_argument("truncated checkpoint RNG state");
    in >> std::ws;
    if (in.peek() != std::char_traits<char>::eof()) throw std::invalid_argument("trailing data in checkpoint");
    return step;
}
}

StratifiedSolver::HandAudit StratifiedSolver::evaluate_hand(const SampledStrategy& strategy, size_t node,
        int hand, uint64_t samples, uint64_t seed, unsigned threads, double confidence) const {
    strategy.validate();
    if (strategy.players != game_.players || strategy.params.stack != game_.params.stack
        || strategy.params.sb_blind != game_.params.sb_blind || strategy.params.bb_blind != game_.params.bb_blind
        || !(strategy.rake == game_.rake))
        throw std::invalid_argument("strategy rules differ from evaluator");
    if (node >= strategy.nodes.size() || hand < 0 || hand >= NUM_HAND_CLASSES || samples < 2
        || samples > 1000000000000ULL || !std::isfinite(confidence) || confidence <= 0 || confidence >= 1)
        throw std::invalid_argument("invalid focused audit request");
    Workspace work(game_, seed, threads);
    Policy policy;
    for (const auto& n : strategy.nodes) policy.push_back(n.frequency);
    work.set_policy(policy);
    const auto& leaves = work.leaves[node];
    const auto per_lane = std::max(uint64_t{1}, (samples + leaves.size() * LANES - 1) / (leaves.size() * LANES));
    const auto count = per_lane * LANES;
    std::array<std::array<Moments, 8>, LANES> lanes{};
    const auto seat = game_.nodes[node].seat;
    work.pool.parallel_for(LANES, [&](int lane) {
        for (size_t l = 0; l < leaves.size(); ++l)
            for (uint64_t k = 0; k < per_lane; ++k)
                lanes[lane][l].add(work.sample(leaves[l], seat, hand, work.rng[lane]));
    });
    std::array<Moments, 8> moments{};
    for (const auto& lane : lanes) for (size_t l = 0; l < leaves.size(); ++l) {
        auto& m = moments[l]; const auto& v = lane[l];
        m.x += v.x; m.x2 += v.x2; m.w += v.w; m.w2 += v.w2; m.xw += v.xw;
    }
    // Fixed-budget fresh audit: union only over this hand's continuation strata.
    // Not a simultaneous guarantee for other hands or repeated, selected runs.
    const double log_term = std::log(8.0 * leaves.size() / (1 - confidence));
    double x = 0, w = 0, w2 = 0, xl = 0, xu = 0, wl = 0, wu = 0;
    for (size_t l = 0; l < leaves.size(); ++l) {
        const auto& m = moments[l];
        const double bound = work.weight_bound(leaves[l], seat, hand);
        const double low = std::min(0.0, leaves[l].lower) * bound, high = std::max(0.0, leaves[l].upper) * bound;
        const double rx = radius(m.x, m.x2, count, high - low, log_term);
        const double rw = radius(m.w, m.w2, count, bound, log_term);
        xl += std::max(low, m.x / count - rx); xu += std::min(high, m.x / count + rx);
        wl += std::max(0.0, m.w / count - rw); wu += std::min(bound, m.w / count + rw);
        x += m.x / count; w += m.w / count; w2 += m.w2;
    }
    const double delta = w > 0 ? x / w : 0;
    double var = 0;
    for (size_t l = 0; l < leaves.size(); ++l) {
        const auto& m = moments[l];
        var += variance(m.x - delta * m.w, m.x2 - 2 * delta * m.xw + delta * delta * m.w2, count) / count;
    }
    HandAudit result;
    result.samples = count * leaves.size(); result.seed = seed; result.confidence = confidence;
    result.reach = std::clamp(w, 0.0, 1.0);
    result.effective_samples = w2 > 0 ? (count * w) * (count * w) / w2 : 0;
    result.fold_ev = -game_.blind(seat); result.action_ev = result.fold_ev + delta;
    result.std_error = w > 0 ? std::sqrt(var) / w : 0;
    const double min_delta = -game_.params.stack + game_.blind(seat);
    const double max_delta = (game_.players - 1) * game_.params.stack + game_.blind(seat);
    wl = std::min(wl, 1.0); wu = std::min(wu, 1.0);
    if (wl > 0 && wu >= wl) {
        result.lower = std::max(min_delta, std::min({xl / wl, xl / wu, xu / wl, xu / wu}));
        result.upper = std::min(max_delta, std::max({xl / wl, xl / wu, xu / wl, xu / wu}));
    } else {
        result.lower = wu > 0 ? min_delta : 0;
        result.upper = wu > 0 ? max_delta : 0;
    }
    return result;
}

uint64_t StratifiedSolver::strata() const {
    uint64_t result = 0;
    for (const auto& node : game_.nodes) result += uint64_t{NUM_HAND_CLASSES} << (game_.players - node.seat - 1);
    return result;
}

SampledStrategy StratifiedSolver::solve(const Config& input) const {
    Config cfg = input;
    if (static_cast<unsigned>(cfg.discounting) > 2 || cfg.schedule_sweeps > 1000000000000ULL
        || (cfg.schedule_sweeps && cfg.discounting != Discounting::HS)
        || !cfg.iterations || cfg.iterations > 1000000000000ULL || !cfg.samples_per_stratum
        || cfg.samples_per_stratum > 1024 || cfg.evaluation_samples < 2 || cfg.audit_samples < 2
        || !std::isfinite(cfg.confidence) || cfg.confidence <= 0 || cfg.confidence >= 1)
        throw std::invalid_argument("invalid stratified training configuration");
    const auto sweep_samples = strata() * cfg.samples_per_stratum;
    const auto sweeps = (cfg.iterations + sweep_samples - 1) / sweep_samples;
    const auto total = sweeps * sweep_samples;
    const size_t nodes = game_.nodes.size(), cells = nodes * NUM_HAND_CLASSES;
    Policy policy(nodes), regrets_a(nodes), regrets_f(nodes), average(nodes);
    for (auto& row : policy) row.fill(0.5);
    Workspace work(game_, cfg.seed, cfg.threads);
    const auto start = Clock::now();
    uint64_t logged = 0;
    double average_weight = 0;
    const uint64_t resumed = cfg.resume_path.empty() ? 0
        : restore(cfg.resume_path, game_, cfg, average_weight, policy, regrets_a, regrets_f, average, work);
    if (resumed > sweeps) throw std::invalid_argument("resume budget must not be below completed checkpoint samples");
    const auto checkpoint_path = cfg.checkpoint_path.empty() ? cfg.resume_path : cfg.checkpoint_path;
    if (cfg.discounting == Discounting::HS && !cfg.schedule_sweeps) cfg.schedule_sweeps = sweeps;
    uint64_t saved = resumed * sweep_samples;
    for (uint64_t step = resumed + 1; step <= sweeps; ++step) {
        work.set_policy(policy);
        const auto update = RegretUpdate::at(step, cfg.linear_weighting, cfg.discounting, cfg.schedule_sweeps);
        work.pool.parallel_for(LANES, [&](int lane) {
            for (size_t cell = lane; cell < cells; cell += LANES) {
                const auto node = cell / NUM_HAND_CLASSES;
                const int h = static_cast<int>(cell % NUM_HAND_CLASSES), seat = game_.nodes[node].seat;
                double delta = 0;
                for (const auto& leaf : work.leaves[node])
                    for (unsigned k = 0; k < cfg.samples_per_stratum; ++k)
                        delta += work.sample(leaf, seat, h, work.rng[lane]).advantage;
                delta /= cfg.samples_per_stratum;
                const double f = policy[node][h];
                // Equal class stratification removes the fixed chance factor
                // P(own class) from BOTH regrets; this does not change matching.
                // Signed LCFR regrets are never clipped after a noisy draw.
                auto& a = regrets_a[node][h]; auto& b = regrets_f[node][h];
                update.apply(f, delta, a, b, average[node][h]);
                const double ap = std::max(0.0, a), bp = std::max(0.0, b);
                policy[node][h] = ap + bp > 0 ? ap / (ap + bp) : 0.5;
            }
        });
        average_weight = update.average * average_weight + update.increment;
        const uint64_t done = step * sweep_samples;
        if (!checkpoint_path.empty() && (step == 1 || step == sweeps || done - saved >= cfg.checkpoint_every)) {
            checkpoint(checkpoint_path, game_, cfg, step, average_weight, policy, regrets_a, regrets_f, average, work);
            saved = done;
        }
        if (cfg.on_progress && (step == sweeps || (cfg.log_every && done - logged >= cfg.log_every))) {
            cfg.on_progress({done, total, std::chrono::duration<double>(Clock::now() - start).count(), false});
            logged = done;
        }
    }
    SampledStrategy result;
    result.players = game_.players; result.params = game_.params; result.rake = game_.rake;
    result.iterations = total; result.seed = cfg.seed; result.training_sweeps = sweeps;
    result.training_method = cfg.discounting == Discounting::DCFR ? 3 : cfg.discounting == Discounting::HS ? 4 : cfg.linear_weighting ? 2 : 1;
    for (size_t node = 0; node < nodes; ++node) {
        SampledNode n; n.decision = game_.nodes[node];
        for (int h = 0; h < NUM_HAND_CLASSES; ++h) n.frequency[h] = average[node][h] / average_weight;
        result.nodes.push_back(n);
    }
    evaluate(result, cfg.evaluation_samples, cfg.audit_samples, cfg.seed ^ 0xd1b54a32d192ed03ULL,
             cfg.threads, cfg.confidence, cfg.on_progress);
    return result;
}

void StratifiedSolver::evaluate(SampledStrategy& strategy, uint64_t global_samples, uint64_t audit_samples,
                               uint64_t seed, unsigned threads, double confidence,
                               const std::function<void(const SampledProgress&)>& progress) const {
    if (audit_samples < 2 || audit_samples > 1000000000000ULL || !std::isfinite(confidence)
        || confidence <= 0 || confidence >= 1)
        throw std::invalid_argument("invalid stratified evaluation configuration");
    // Independent UNIFORM deals retain an implementation-independent cross-check
    // of aggregate EV and chip conservation. The conditional evaluator below
    // uses a different estimator and fresh random streams.
    SampledSolver(game_).evaluate(strategy, global_samples, seed, threads, progress);
    const uint64_t count = std::max(uint64_t{2}, (audit_samples + strata() - 1) / strata());
    const uint64_t total = count * strata();
    const size_t cells = game_.nodes.size() * NUM_HAND_CLASSES;
    Policy policy;
    for (const auto& node : strategy.nodes) policy.push_back(node.frequency);
    Workspace work(game_, seed ^ 0x94d049bb133111ebULL, threads);
    work.set_policy(policy);
    std::vector<std::array<Moments, 8>> moments(cells);
    const auto start = Clock::now();
    // Bounded chunks let the GUI receive progress during long precision audits.
    for (uint64_t done = 0; done < count;) {
        const auto batch = std::min(uint64_t{64}, count - done);
        work.pool.parallel_for(LANES, [&](int lane) {
            for (size_t cell = lane; cell < cells; cell += LANES) {
                const size_t node = cell / NUM_HAND_CLASSES;
                const int hand = static_cast<int>(cell % NUM_HAND_CLASSES), seat = game_.nodes[node].seat;
                for (size_t leaf = 0; leaf < work.leaves[node].size(); ++leaf)
                    for (uint64_t k = 0; k < batch; ++k)
                        moments[cell][leaf].add(work.sample(work.leaves[node][leaf], seat, hand, work.rng[lane]));
            }
        });
        done += batch;
        if (progress) progress({done * strata(), total,
            std::chrono::duration<double>(Clock::now() - start).count(), true, true});
    }
    const double log_term = std::log(8.0 * strata() / (1 - confidence));
    strategy.sampled_nash_conv = 0;
    strategy.deviation_upper = 0;
    strategy.audit_samples = total;
    strategy.confidence = confidence;
    std::array<double, 4> ev_variance{};
    for (int p = 0; p < game_.players; ++p) strategy.conditional_ev[p] = -game_.blind(p);
    const int bb = game_.players - 1;
    const double walk_bonus = game_.settle(0, {}).ev[bb] + game_.blind(bb);
    strategy.conditional_ev[bb] += walk_bonus;
    for (size_t cell = 0; cell < cells; ++cell) {
        const size_t node = cell / NUM_HAND_CLASSES;
        const int h = static_cast<int>(cell % NUM_HAND_CLASSES), seat = game_.nodes[node].seat;
        double x = 0, w = 0, w2 = 0, xl = 0, xu = 0, wl = 0, wu = 0;
        for (size_t leaf = 0; leaf < work.leaves[node].size(); ++leaf) {
            const auto& m = moments[cell][leaf]; const auto& l = work.leaves[node][leaf];
            const double bound = work.weight_bound(l, seat, h);
            const double low = std::min(0.0, l.lower) * bound, high = std::max(0.0, l.upper) * bound;
            const double rx = radius(m.x, m.x2, count, high - low, log_term);
            const double rw = radius(m.w, m.w2, count, bound, log_term);
            xl += std::max(low, m.x / count - rx); xu += std::min(high, m.x / count + rx);
            wl += std::max(0.0, m.w / count - rw); wu += std::min(bound, m.w / count + rw);
            x += m.x / count; w += m.w / count; w2 += m.w2;
        }
        auto& out = strategy.nodes[node];
        const double delta = w > 0 ? x / w : 0;
        double var = 0;
        for (size_t leaf = 0; leaf < work.leaves[node].size(); ++leaf) {
            const auto& m = moments[cell][leaf];
            var += variance(m.x - delta * m.w,
                m.x2 - 2 * delta * m.xw + delta * delta * m.w2, count) / count;
        }
        out.fold_ev[h] = -game_.blind(seat);
        out.action_ev[h] = delta + out.fold_ev[h];
        out.action_ev_std_error[h] = w > 0 ? std::sqrt(var) / w : 0;
        out.reach[h] = std::clamp(w, 0.0, 1.0);
        out.effective_samples[h] = w2 > 0 ? (count * w) * (count * w) / w2 : 0;
        const double min_delta = -game_.params.stack + game_.blind(seat);
        const double max_delta = (game_.players - 1) * game_.params.stack + game_.blind(seat);
        wl = std::min(wl, 1.0); wu = std::min(wu, 1.0);
        if (wl > 0 && wu >= wl) {
            out.advantage_lower[h] = std::max(min_delta, std::min({xl / wl, xl / wu, xu / wl, xu / wu}));
            out.advantage_upper[h] = std::min(max_delta, std::max({xl / wl, xl / wu, xu / wl, xu / wu}));
        } else {
            out.advantage_lower[h] = wu > 0 ? min_delta : 0;
            out.advantage_upper[h] = wu > 0 ? max_delta : 0;
        }
        // Each player acts once. Its exact best response decomposes over its
        // information sets; importantly, maximize AFTER averaging the samples.
        const double f = out.frequency[h], chance = HandClass::from_index(h).num_combos() / 1326.0;
        const double bonus = seat == bb ? walk_bonus : 0;
        strategy.conditional_ev[seat] += chance * (f * x - bonus * w);
        for (size_t leaf = 0; leaf < work.leaves[node].size(); ++leaf) {
            const auto& m = moments[cell][leaf];
            ev_variance[seat] += chance * chance * variance(f * m.x - bonus * m.w,
                f * f * m.x2 - 2 * f * bonus * m.xw + bonus * bonus * m.w2, count) / count;
        }
        auto gain = [f](double d) { return std::max(0.0, d) - f * d; };
        strategy.sampled_nash_conv += chance * gain(x);
        strategy.deviation_upper += chance * std::max(gain(xl), gain(xu));
    }
    for (int p = 0; p < game_.players; ++p) strategy.conditional_ev_std_error[p] = std::sqrt(ev_variance[p]);
    strategy.validate();
}
}
