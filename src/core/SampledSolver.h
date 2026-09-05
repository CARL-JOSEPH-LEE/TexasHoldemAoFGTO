#pragma once

#include "HandClass.h"
#include "MultiwayGame.h"
#include <functional>

namespace aof2 {
using HandValues = std::array<double, NUM_HAND_CLASSES>;

struct SampledNode {
    DecisionNode decision;
    HandValues frequency{};
    HandValues action_ev{};
    HandValues fold_ev{};
    HandValues reach{};  // probability of history conditional on own hand
    HandValues effective_samples{};
    HandValues action_ev_std_error{};
    HandValues advantage_lower{}, advantage_upper{}; // simultaneous conditional EV-difference interval
};

struct SampledStrategy {
    int players = 4;
    GameParams params;
    RakeRules rake;
    uint64_t iterations = 0, seed = 0, evaluation_samples = 0;
    std::array<double, 4> ev{}, ev_std_error{};
    double expected_rake = 0, sampled_nash_conv = 0;
    uint32_t training_method = 0; // 0: chance CFR, 1: CFR, 2: LCFR, 3: DCFR, 4: HS-DCFR(30)
    uint64_t training_sweeps = 0, audit_samples = 0;
    double confidence = 0, deviation_upper = 0;
    std::array<double, 4> conditional_ev{}, conditional_ev_std_error{};
    std::vector<SampledNode> nodes;

    void validate() const;
    void save(const std::string& path) const;
    void load(const std::string& path);
    void export_csv(const std::string& path) const;
};

struct SampledProgress {
    uint64_t completed = 0, total = 0;
    double seconds = 0;
    bool evaluating = false;
    bool auditing = false;
};

class SampledSolver {
public:
    struct Config {
        uint64_t iterations = 2000000; // independent deals, not full table sweeps
        uint64_t seed = 2026;
        uint64_t evaluation_samples = 200000;
        unsigned threads = 0;
        uint64_t log_every = 100000;
        std::function<void(const SampledProgress&)> on_progress;
    };
    explicit SampledSolver(MultiwayGame game) : game_(std::move(game)) {}
    SampledStrategy solve(const Config& config) const;
    void evaluate(SampledStrategy& strategy, uint64_t samples, uint64_t seed,
                  unsigned threads = 0,
                  const std::function<void(const SampledProgress&)>& progress = {}) const;
private:
    MultiwayGame game_;
};
}
