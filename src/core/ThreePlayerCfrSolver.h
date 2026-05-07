#pragma once

#include "EquityTable.h"
#include "PushFoldGame.h"
#include "ThreePlayerStrategy.h"
#include "ThreeWayEquityTable.h"

#include <cstdint>
#include <functional>
#include <string>

namespace aof2 {

struct CfrProgress3
{
    uint64_t iteration = 0;
    double btn_ev = 0.0;
    double sb_ev = 0.0;
    double bb_ev = 0.0;
    double exploitability_bb = 0.0;
    double seconds = 0.0;
};

class ThreePlayerCfrSolver
{
public:
    struct Config
    {
        uint64_t iterations = 200000;
        unsigned threads = 0;
        uint64_t log_every = 1000;
        std::function<void(const CfrProgress3&)> on_progress;
    };

    ThreePlayerCfrSolver(const EquityTable& eq2,
                         const ThreeWayEquityTable& eq3,
                         const GameParams& params);

    ThreePlayerStrategy solve(const Config& cfg);

private:
    void compute_evs_(const std::array<double, NUM_HAND_CLASSES>& btn_push,
                      const std::array<double, NUM_HAND_CLASSES>& sb_call_vs_btn_push,
                      const std::array<double, NUM_HAND_CLASSES>& sb_push_vs_btn_fold,
                      const std::array<double, NUM_HAND_CLASSES>& bb_call_3way,
                      const std::array<double, NUM_HAND_CLASSES>& bb_call_vs_btn_only,
                      const std::array<double, NUM_HAND_CLASSES>& bb_call_vs_sb_only,
                      ThreePlayerStrategy& out,
                      unsigned threads) const;

    double exploitability_(const ThreePlayerStrategy& s, unsigned threads) const;

    const EquityTable& m_eq2;
    const ThreeWayEquityTable& m_eq3;
    GameParams m_params;
    int m_combos[NUM_HAND_CLASSES];
};

}
