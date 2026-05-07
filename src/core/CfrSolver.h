#pragma once

#include "EquityTable.h"
#include "PushFoldGame.h"
#include "Strategy.h"

#include <cstdint>
#include <functional>
#include <string>

namespace aof2 {

struct CfrProgress
{
    uint64_t iteration = 0;
    double sb_ev = 0.0;
    double exploitability_bb = 0.0;
    double seconds = 0.0;
};

class CfrSolver
{
public:
    struct Config
    {
        uint64_t iterations = 200000;
        unsigned threads = 0;
        uint64_t log_every = 1000;
        std::function<void(const CfrProgress&)> on_progress;
    };

    CfrSolver(const EquityTable& equity, const GameParams& params);

    Strategy solve(const Config& cfg);

    double exploitability(const std::array<double, NUM_HAND_CLASSES>& sb_push,
                          const std::array<double, NUM_HAND_CLASSES>& bb_call) const;

    double sb_ev(const std::array<double, NUM_HAND_CLASSES>& sb_push,
                 const std::array<double, NUM_HAND_CLASSES>& bb_call) const;

    struct HandEvBreakdown
    {
        double sb_push_ev[NUM_HAND_CLASSES];
        double sb_fold_ev;
        double bb_call_ev[NUM_HAND_CLASSES];
        double bb_fold_ev;
    };

    HandEvBreakdown breakdown(const std::array<double, NUM_HAND_CLASSES>& sb_push,
                              const std::array<double, NUM_HAND_CLASSES>& bb_call) const;

private:
    const EquityTable& m_eq;
    GameParams m_params;
    int m_combos[NUM_HAND_CLASSES];
};

}
