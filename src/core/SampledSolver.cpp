#include "SampledSolver.h"
#include "ThreadPool.h"
#include <omp/HandEvaluator.h>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>

namespace aof2 {
namespace {
constexpr unsigned LANES = 8;
constexpr uint64_t BATCH = 2048;
using Policy = std::vector<HandValues>;
using Value = std::array<double, 5>; // four player EVs, then rake

struct Cell {
    double action = 0, fold = 0, weight = 0, weight_squared = 0;
    uint64_t visits = 0;
};
struct Stats {
    std::vector<std::array<Cell, NUM_HAND_CLASSES>> nodes;
    Value sum{}, squared{};
    explicit Stats(size_t n) : nodes(n) {}
    void clear() {
        for (auto& row : nodes) row.fill(Cell{});
        sum.fill(0); squared.fill(0);
    }
};
struct Deal {
    std::array<int, 4> hands{};
    std::array<Value, 16> terminal{};
};

uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Deal draw(const MultiwayGame& game, std::mt19937_64& rng, const omp::HandEvaluator& evaluator) {
    std::array<uint8_t, 52> deck{};
    std::iota(deck.begin(), deck.end(), uint8_t{0});
    const int holes = 2 * game.players;
    for (int i = 0; i < holes + 5; ++i) {
        const auto j = std::uniform_int_distribution<int>(i, 51)(rng);
        std::swap(deck[i], deck[j]);
    }
    omp::Hand board = omp::Hand::empty();
    for (int i = holes; i < holes + 5; ++i) board += omp::Hand(deck[i]);
    Deal deal;
    std::array<uint16_t, 4> ranks{};
    for (int p = 0; p < game.players; ++p) {
        const auto a = deck[2 * p], b = deck[2 * p + 1];
        deal.hands[p] = HandClass::from_combo(a, b).index();
        ranks[p] = evaluator.evaluate(board + omp::Hand(a) + omp::Hand(b));
    }
    // Folded hole cards remain removed from the board in every branch.
    for (unsigned mask = 0; mask < (1u << game.players); ++mask) {
        const auto result = game.settle(mask, ranks);
        for (int p = 0; p < 4; ++p) deal.terminal[mask][p] = result.ev[p];
        deal.terminal[mask][4] = result.rake;
    }
    return deal;
}

Value walk(const MultiwayGame& game, const Deal& deal, const Policy& policy,
           int seat, unsigned mask, double reach, Stats& stats, bool training) {
    if (seat == game.players || (seat == game.players - 1 && mask == 0))
        return deal.terminal[mask];
    const int node = game.node_index[seat][mask];
    const int hand = deal.hands[seat];
    const double f = policy[node][hand];
    const auto fold = walk(game, deal, policy, seat + 1, mask, reach * (1 - f), stats, training);
    const auto action = walk(game, deal, policy, seat + 1, mask | (1u << seat), reach * f, stats, training);
    Value value{};
    for (size_t p = 0; p < value.size(); ++p) value[p] = f * action[p] + (1 - f) * fold[p];
    auto& cell = stats.nodes[node][hand];
    // Each seat acts once: reach consists entirely of opponents' actions.
    // Retain this factor; dividing by the observed reach would bias CFR updates.
    cell.action += reach * (action[seat] - (training ? value[seat] : 0));
    cell.fold += reach * (fold[seat] - (training ? value[seat] : 0));
    if (!training) {
        cell.weight += reach;
        cell.weight_squared += reach * reach;
        ++cell.visits;
    }
    return value;
}

unsigned worker_count(unsigned requested) {
    if (requested > 256) throw std::invalid_argument("threads must be <= 256");
    const auto n = requested ? requested : std::thread::hardware_concurrency();
    return std::max(1u, std::min(LANES, n));
}

struct Workspace {
    omp::HandEvaluator evaluator; // initialize OMPEval before starting workers
    ThreadPool pool;
    std::vector<Stats> lanes;
    std::array<std::mt19937_64, LANES> rng;
    Workspace(size_t nodes, uint64_t seed, unsigned threads) : pool(worker_count(threads)) {
        for (unsigned i = 0; i < LANES; ++i) {
            lanes.emplace_back(nodes);
            rng[i].seed(mix(seed + i));
        }
    }
    void batch(const MultiwayGame& game, const Policy& policy, uint64_t count, bool training) {
        // Fixed logical lanes and reduction order make a seed reproducible across thread counts.
        pool.parallel_for(LANES, [&](int lane) {
            auto& stat = lanes[lane];
            if (training) stat.clear();
            const uint64_t n = count / LANES + (static_cast<unsigned>(lane) < count % LANES);
            for (uint64_t i = 0; i < n; ++i) {
                const auto deal = draw(game, rng[lane], evaluator);
                const auto result = walk(game, deal, policy, 0, 0, 1.0, stat, training);
                if (!training) for (size_t p = 0; p < result.size(); ++p) {
                    stat.sum[p] += result[p];
                    stat.squared[p] += result[p] * result[p];
                }
            }
        });
    }
};
}

SampledStrategy SampledSolver::solve(const Config& cfg) const {
    if (cfg.iterations == 0 || cfg.evaluation_samples < 2)
        throw std::invalid_argument("iterations must be positive; evaluation samples must be >= 2");
    const size_t n = game_.nodes.size();
    Policy sigma(n), regret_action(n), regret_fold(n), average(n);
    for (auto& row : sigma) row.fill(0.5);
    Workspace work(n, cfg.seed, cfg.threads);
    const auto start = std::chrono::steady_clock::now();
    uint64_t logged = 0;
    for (uint64_t done = 0; done < cfg.iterations;) {
        const auto count = std::min(BATCH, cfg.iterations - done);
        work.batch(game_, sigma, count, true);
        for (size_t node = 0; node < n; ++node) for (int h = 0; h < NUM_HAND_CLASSES; ++h) {
            // Own reach is one at every information set. Average the policies
            // actually used, not policies weighted by opponents' history reach.
            average[node][h] += sigma[node][h] * count;
            for (const auto& lane : work.lanes) {
                regret_action[node][h] += lane.nodes[node][h].action;
                regret_fold[node][h] += lane.nodes[node][h].fold;
            }
            // Standard sampled CFR keeps signed cumulative regrets. Clipping the
            // regrets after each noisy sample introduces unnecessary positive bias.
            const double a = std::max(0.0, regret_action[node][h]);
            const double f = std::max(0.0, regret_fold[node][h]);
            sigma[node][h] = a + f > 0 ? a / (a + f) : 0.5;
        }
        done += count;
        if (cfg.on_progress && (done == cfg.iterations || (cfg.log_every && done - logged >= cfg.log_every))) {
            cfg.on_progress({done, cfg.iterations,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), false});
            logged = done;
        }
    }
    SampledStrategy result;
    result.players = game_.players;
    result.params = game_.params;
    result.rake = game_.rake;
    result.iterations = cfg.iterations;
    result.seed = cfg.seed;
    for (size_t node = 0; node < n; ++node) {
        SampledNode out;
        out.decision = game_.nodes[node];
        for (int h = 0; h < NUM_HAND_CLASSES; ++h) out.frequency[h] = average[node][h] / cfg.iterations;
        result.nodes.push_back(out);
    }
    evaluate(result, cfg.evaluation_samples, cfg.seed ^ 0xd1b54a32d192ed03ULL, cfg.threads, cfg.on_progress);
    return result;
}

void SampledSolver::evaluate(SampledStrategy& strategy, uint64_t samples, uint64_t seed,
                            unsigned threads, const std::function<void(const SampledProgress&)>& progress) const {
    if (samples < 2) throw std::invalid_argument("evaluation requires at least 2 samples");
    strategy.validate();
    if (strategy.players != game_.players || strategy.params.stack != game_.params.stack
        || strategy.params.sb_blind != game_.params.sb_blind || strategy.params.bb_blind != game_.params.bb_blind
        || strategy.rake.rate != game_.rake.rate || strategy.rake.cap != game_.rake.cap
        || strategy.rake.no_flop_no_drop != game_.rake.no_flop_no_drop)
        throw std::invalid_argument("strategy rules do not match evaluator rules");
    strategy.audit_samples = 0; strategy.confidence = 0; strategy.deviation_upper = 0;
    strategy.conditional_ev.fill(0); strategy.conditional_ev_std_error.fill(0);
    for (auto& node : strategy.nodes) {
        node.action_ev_std_error.fill(0); node.advantage_lower.fill(0); node.advantage_upper.fill(0);
    }
    Policy policy;
    for (const auto& node : strategy.nodes) policy.push_back(node.frequency);
    Workspace work(policy.size(), seed, threads);
    const auto start = std::chrono::steady_clock::now();
    uint64_t logged = 0;
    for (uint64_t done = 0; done < samples;) {
        const auto count = std::min(uint64_t{8192}, samples - done);
        work.batch(game_, policy, count, false);
        done += count;
        if (progress && (done == samples || done - logged >= 100000)) {
            progress({done, samples,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), true});
            logged = done;
        }
    }
    Value sum{}, squared{};
    for (const auto& lane : work.lanes) for (size_t p = 0; p < sum.size(); ++p) {
        sum[p] += lane.sum[p]; squared[p] += lane.squared[p];
    }
    for (int p = 0; p < 4; ++p) {
        strategy.ev[p] = sum[p] / samples;
        const double variance = std::max(0.0, (squared[p] - sum[p] * sum[p] / samples) / (samples - 1));
        strategy.ev_std_error[p] = std::sqrt(variance / samples);
    }
    strategy.evaluation_samples = samples;
    strategy.expected_rake = sum[4] / samples;
    strategy.sampled_nash_conv = 0;
    for (size_t node = 0; node < strategy.nodes.size(); ++node) for (int h = 0; h < NUM_HAND_CLASSES; ++h) {
        Cell total;
        for (const auto& lane : work.lanes) {
            const auto& c = lane.nodes[node][h];
            total.action += c.action; total.fold += c.fold;
            total.weight += c.weight; total.weight_squared += c.weight_squared; total.visits += c.visits;
        }
        auto& out = strategy.nodes[node];
        out.action_ev[h] = total.weight > 0 ? total.action / total.weight : 0;
        out.fold_ev[h] = total.weight > 0 ? total.fold / total.weight : 0;
        out.reach[h] = total.visits ? total.weight / total.visits : 0;
        out.effective_samples[h] = total.weight_squared > 0 ? total.weight * total.weight / total.weight_squared : 0;
        const double current = out.frequency[h] * total.action + (1 - out.frequency[h]) * total.fold;
        strategy.sampled_nash_conv += std::max(0.0, std::max(total.action, total.fold) - current) / samples;
    }
    strategy.validate();
}
}
