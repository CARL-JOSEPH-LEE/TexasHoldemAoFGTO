#pragma once

#include "PushFoldGame.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aof2 {

struct RakeRules {
    double rate = 0.0;     // fraction, e.g. 0.03 = 3 percent
    double cap = 0.0;      // BB; zero means unlimited
    bool no_flop_no_drop = true;

    void validate() const {
        if (!std::isfinite(rate) || rate < 0 || rate > 1 || !std::isfinite(cap) || cap < 0)
            throw std::invalid_argument("rake rate must be in [0,1]; cap must be finite and >= 0");
    }
    double amount(double contested_pot, bool showdown) const {
        if (!showdown && no_flop_no_drop) return 0;
        const double uncapped = contested_pot * rate;
        return cap > 0 ? std::min(uncapped, cap) : uncapped;
    }
};

struct DecisionNode {
    int seat = 0;
    unsigned prior_mask = 0; // bit i: earlier seat i is all-in; absent bit: folded
};

struct Settlement {
    std::array<double, 4> ev{};
    double pot = 0;         // contested pot, excludes uncalled excess
    double rake = 0;
};

class MultiwayGame {
public:
    MultiwayGame(int players, GameParams params = {}, RakeRules rake = {})
        : players(players), params(params), rake(rake) {
        if (players < 2 || players > 4) throw std::invalid_argument("players must be 2, 3 or 4");
        params.validate();
        rake.validate();
        for (auto& row : node_index) row.fill(-1);
        for (int seat = 0; seat < players; ++seat) {
            for (unsigned mask = 0; mask < (1u << seat); ++mask) {
                if (seat == players - 1 && mask == 0) continue; // BB walks
                node_index[seat][mask] = static_cast<int>(nodes.size());
                nodes.push_back({seat, mask});
            }
        }
    }

    double blind(int seat) const {
        return seat == players - 1 ? params.bb_blind : (seat == players - 2 ? params.sb_blind : 0.0);
    }
    std::string position(int seat) const {
        if (seat == players - 1) return "BB";
        if (seat == players - 2) return "SB";
        return seat == players - 3 ? "BTN" : "CO";
    }
    std::string title(const DecisionNode& node) const {
        std::string s = position(node.seat) + (node.prior_mask ? " CALL" : " ALL IN");
        if (node.seat) {
            s += " | ";
            for (int i = 0; i < node.seat; ++i) {
                if (i) s += ", ";
                s += position(i) + ((node.prior_mask & (1u << i)) ? " all-in" : " fold");
            }
        }
        return s;
    }

    // All players have equal stacks including posted blinds. Every active subset
    // is settled directly, so folded blinds and split pots are included exactly.
    Settlement settle(unsigned active, const std::array<uint16_t, 4>& ranks) const {
        if (active >= (1u << players)) throw std::invalid_argument("invalid active mask");
        if (active == 0) active = 1u << (players - 1);
        Settlement out;
        int count = 0, sole = -1;
        for (int p = 0; p < players; ++p) {
            if (active & (1u << p)) { ++count; sole = p; }
            out.ev[p] = -blind(p);
        }
        if (count == 1) {
            double dead = 0, matched = 0;
            for (int p = 0; p < players; ++p) if (p != sole) {
                dead += blind(p);
                matched = std::max(matched, blind(p));
            }
            out.pot = dead + matched;
            out.rake = rake.amount(out.pot, false);
            out.ev[sole] = dead - out.rake;
            return out;
        }
        uint16_t best = 0;
        int winners = 0;
        for (int p = 0; p < players; ++p) {
            if (active & (1u << p)) {
                out.ev[p] = -params.stack;
                best = std::max(best, ranks[p]);
            }
            out.pot -= out.ev[p];
        }
        for (int p = 0; p < players; ++p)
            if ((active & (1u << p)) && ranks[p] == best) ++winners;
        out.rake = rake.amount(out.pot, true);
        for (int p = 0; p < players; ++p)
            if ((active & (1u << p)) && ranks[p] == best)
                out.ev[p] += (out.pot - out.rake) / winners;
        return out;
    }

    int players;
    GameParams params;
    RakeRules rake;
    std::vector<DecisionNode> nodes;
    std::array<std::array<int, 8>, 4> node_index{};
};
}
