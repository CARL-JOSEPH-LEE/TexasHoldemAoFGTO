#pragma once

#include "HandClass.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace aof2 {

// A distribution over physical two-card combinations, not over 169 equally
// likely labels. Collision conditioning is exact; no capped rejection bias.
class RangeSampler {
public:
    using Values = std::array<double, NUM_HAND_CLASSES>;
    explicit RangeSampler(const Values& frequencies = {}, bool action = true) {
        reset(frequencies, action);
    }
    void reset(const Values& frequencies, bool action) {
        total_ = 0;
        card_mass_.fill(0);
        for (int h = 0; h < NUM_HAND_CLASSES; ++h) {
            weights_[h] = action ? frequencies[h] : 1 - frequencies[h];
            if (!std::isfinite(weights_[h]) || weights_[h] < 0 || weights_[h] > 1)
                throw std::invalid_argument("invalid range frequency");
            const auto& combos = combinations()[h];
            total_ += weights_[h] * combos.size();
            for (const auto c : combos) {
                card_mass_[c.card_high] += weights_[h];
                card_mass_[c.card_low] += weights_[h];
            }
        }
        std::array<int, NUM_HAND_CLASSES> small{}, large{};
        int ns = 0, nl = 0;
        for (int h = 0; h < NUM_HAND_CLASSES; ++h) {
            alias_[h] = h;
            probability_[h] = total_ > 0 ? weights_[h] * combinations()[h].size() * NUM_HAND_CLASSES / total_ : 0;
            (probability_[h] < 1 ? small[ns++] : large[nl++]) = h;
        }
        while (ns && nl) {
            const int s = small[--ns], l = large[--nl];
            alias_[s] = l;
            probability_[l] -= 1 - probability_[s];
            (probability_[l] < 1 ? small[ns++] : large[nl++]) = l;
        }
        while (ns) probability_[small[--ns]] = 1;
        while (nl) probability_[large[--nl]] = 1;
    }
    double total() const { return total_; }
    double weight(Combo c) const { return weights_[class_index(c.card_high, c.card_low)]; }
    double available_mass(const std::array<uint8_t, 8>& blocked, int count) const {
        // Inclusion-exclusion stops at pairs because a hand has only two cards.
        double mass = total_;
        for (int i = 0; i < count; ++i) {
            mass -= card_mass_[blocked[i]];
            for (int j = 0; j < i; ++j) mass += weights_[class_index(blocked[i], blocked[j])];
        }
        if (mass < 1e-9 * total_) {
            // Avoid catastrophic cancellation when nearly all range mass is
            // blocked (e.g. two AA ranges with an AA hero).
            uint64_t mask = 0;
            for (int i = 0; i < count; ++i) mask |= uint64_t{1} << blocked[i];
            mass = 0;
            for (int h = 0; h < NUM_HAND_CLASSES; ++h) if (weights_[h] > 0)
                for (auto c : combinations()[h]) if (!(bits(c) & mask)) mass += weights_[h];
        }
        return std::max(0.0, mass);
    }
    Combo sample(uint64_t blocked, double available, std::mt19937_64& rng) const {
        if (!(available > 0)) throw std::invalid_argument("cannot sample empty range");
        for (int attempt = 0; attempt < 8; ++attempt) {
            int h = std::uniform_int_distribution<int>(0, NUM_HAND_CLASSES - 1)(rng);
            if (unit(rng) >= probability_[h]) h = alias_[h];
            const auto& combos = combinations()[h];
            const auto c = combos[std::uniform_int_distribution<size_t>(0, combos.size() - 1)(rng)];
            if (!(bits(c) & blocked)) return c;
        }
        // Conditional fallback has the SAME target law as rejection sampling.
        // It terminates even if the legal mass is tiny relative to total mass.
        double target = unit(rng) * available;
        Combo last{};
        for (int h = 0; h < NUM_HAND_CLASSES; ++h) if (weights_[h] > 0) {
            for (auto c : combinations()[h]) if (!(bits(c) & blocked)) {
                last = c;
                target -= weights_[h];
                if (target < 0) return c;
            }
        }
        return last; // roundoff at the upper CDF endpoint
    }
    static uint64_t bits(Combo c) { return (uint64_t{1} << c.card_high) | (uint64_t{1} << c.card_low); }
    static double unit(std::mt19937_64& rng) { return (rng() >> 11) * 0x1.0p-53; }
    static int class_index(uint8_t a, uint8_t b) {
        int hi = a >> 2, lo = b >> 2;
        if (hi == lo) return hi;
        if (hi < lo) std::swap(hi, lo);
        return ((a & 3) == (b & 3) ? 13 : 91) + hi * (hi - 1) / 2 + lo;
    }
    static const std::array<std::vector<Combo>, NUM_HAND_CLASSES>& combinations() {
        static const auto result = [] {
            std::array<std::vector<Combo>, NUM_HAND_CLASSES> out;
            for (int h = 0; h < NUM_HAND_CLASSES; ++h) out[h] = HandClass::from_index(h).enumerate_combos();
            return out;
        }();
        return result;
    }
private:
    Values weights_{}, probability_{};
    std::array<int, NUM_HAND_CLASSES> alias_{};
    std::array<double, NUM_CARDS> card_mass_{};
    double total_ = 0;
};
}
