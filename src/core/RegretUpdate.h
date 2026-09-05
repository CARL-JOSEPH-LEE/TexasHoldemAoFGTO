#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace aof2 {
enum class Discounting : unsigned { None = 0, DCFR = 1, HS = 2 };

// Independent implementation of Brown & Sandholm (2019), and Zhang,
// McAleer & Sandholm, AAAI 2026, equations 2--4. See docs/research.txt.
// These deterministic update rules do not transfer two-player zero-sum
// convergence guarantees to sampled multiplayer or raked games.
struct RegretUpdate {
    double positive = 1, negative = 1, average = 1, increment = 1;
    static RegretUpdate at(uint64_t step, bool linear, Discounting mode, uint64_t horizon = 0) {
        if (!step || static_cast<unsigned>(mode) > 2)
            throw std::invalid_argument("invalid regret update");
        RegretUpdate w;
        if (mode == Discounting::None) { w.increment = linear ? double(step) : 1; return w; }
        if (mode == Discounting::HS && !horizon)
            throw std::invalid_argument("HS-DCFR requires a fixed schedule horizon");
        const double t = double(step - 1);
        const double progress = horizon ? std::min(1.0, t / double(horizon)) : 0;
        const double alpha = mode == Discounting::HS ? 1 + 3 * progress : 1.5;
        const double beta = mode == Discounting::HS ? -1 - 2 * progress : 0;
        const double gamma = mode == Discounting::HS ? 30 - 5 * progress : 2;
        // Logistic form avoids overflow from t^alpha on long runs.
        w.positive = t ? 1 / (1 + std::pow(t, -alpha)) : 0;
        w.negative = t ? 1 / (1 + std::pow(t, -beta)) : 0;
        w.average = std::pow(t / (t + 1), gamma);
        return w;
    }
    void apply(double frequency, double advantage, double& all_in, double& fold, double& sum) const {
        sum = average * sum + increment * frequency;
        all_in *= all_in > 0 ? positive : negative;
        fold *= fold > 0 ? positive : negative;
        all_in += increment * (1 - frequency) * advantage;
        fold -= increment * frequency * advantage;
    }
};
}
