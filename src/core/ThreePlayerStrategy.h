#pragma once

#include "HandClass.h"
#include "PushFoldGame.h"

#include <array>
#include <cstdint>
#include <string>

namespace aof2 {

struct ThreePlayerStrategy
{
    GameParams params;

    std::array<double, NUM_HAND_CLASSES> btn_push{};
    std::array<double, NUM_HAND_CLASSES> sb_call_vs_btn_push{};
    std::array<double, NUM_HAND_CLASSES> sb_push_vs_btn_fold{};
    std::array<double, NUM_HAND_CLASSES> bb_call_3way{};
    std::array<double, NUM_HAND_CLASSES> bb_call_vs_btn_only{};
    std::array<double, NUM_HAND_CLASSES> bb_call_vs_sb_only{};

    std::array<double, NUM_HAND_CLASSES> btn_push_ev{};
    std::array<double, NUM_HAND_CLASSES> sb_call_ev_vs_btn_push{};
    std::array<double, NUM_HAND_CLASSES> sb_push_ev_vs_btn_fold{};
    std::array<double, NUM_HAND_CLASSES> bb_call_ev_3way{};
    std::array<double, NUM_HAND_CLASSES> bb_call_ev_vs_btn_only{};
    std::array<double, NUM_HAND_CLASSES> bb_call_ev_vs_sb_only{};
    double btn_fold_ev = 0.0;
    double sb_fold_ev_vs_btn_push = 0.0;
    double sb_fold_ev_vs_btn_fold = 0.0;
    double bb_fold_ev = 0.0;

    double btn_ev = 0.0;
    double sb_ev = 0.0;
    double bb_ev = 0.0;

    double exploitability_bb = 0.0;
    uint64_t iterations = 0;

    void save(const std::string& path) const;
    void load(const std::string& path);
};

}
