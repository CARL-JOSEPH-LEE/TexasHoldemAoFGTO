#pragma once

#include "SampledSolver.h"
#include "RegretUpdate.h"

namespace aof2 {

class StratifiedSolver {
public:
    struct Config : SampledSolver::Config {
        // A sweep covers every (decision, hand, continuation) stratum.
        // The requested budget is rounded UP to complete sweeps.
        unsigned samples_per_stratum = 4;
        bool linear_weighting = true;
        Discounting discounting = Discounting::None;
        uint64_t schedule_sweeps = 0; // HS horizon, frozen in checkpoint; 0 = initial target
        uint64_t audit_samples = 1000000;
        double confidence = 0.99;
        std::string checkpoint_path, resume_path;
        uint64_t checkpoint_every = 10000000; // sample budget between atomic saves
    };
    explicit StratifiedSolver(MultiwayGame game) : game_(std::move(game)) {}
    SampledStrategy solve(const Config& config) const;
    void evaluate(SampledStrategy& strategy, uint64_t global_samples, uint64_t audit_samples,
                  uint64_t seed, unsigned threads = 0, double confidence = 0.99,
                  const std::function<void(const SampledProgress&)>& progress = {}) const;
    uint64_t strata() const;
    struct HandAudit {
        uint64_t samples = 0, seed = 0;
        double confidence = 0, reach = 0, effective_samples = 0;
        double action_ev = 0, fold_ev = 0, std_error = 0, lower = 0, upper = 0;
    };
    HandAudit evaluate_hand(const SampledStrategy& strategy, size_t node, int hand,
                            uint64_t samples, uint64_t seed, unsigned threads = 0,
                            double confidence = 0.99) const;
private:
    MultiwayGame game_;
};
}
