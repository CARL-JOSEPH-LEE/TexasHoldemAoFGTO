#pragma once

#include "HandClass.h"
#include "PushFoldGame.h"

#include <array>
#include <cstdint>
#include <string>

namespace aof2 {

struct Strategy
{
    GameParams params;

    std::array<double, NUM_HAND_CLASSES> sb_push{};
    std::array<double, NUM_HAND_CLASSES> bb_call{};

    std::array<double, NUM_HAND_CLASSES> sb_push_ev{};
    std::array<double, NUM_HAND_CLASSES> bb_call_ev{};
    double sb_fold_ev = 0.0;
    double bb_fold_ev = 0.0;

    double sb_ev = 0.0;
    double exploitability_bb = 0.0;
    uint64_t iterations = 0;

    void save(const std::string& path) const;
    void load(const std::string& path);
};

}
