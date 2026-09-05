// An independent, slow, rank-counting oracle. It deliberately does not use
// OMPEval's hash tables, rank encodings or flush/straight detection.
#include "core/RegretUpdate.h"
#include <omp/HandEvaluator.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t reference5(const std::array<int, 5>& cards) {
    std::array<int, 13> counts{};
    bool flush = true;
    for (int c : cards) { ++counts[c / 4]; flush &= c % 4 == cards[0] % 4; }
    std::array<std::pair<int, int>, 5> groups{};
    int n = 0, straight = -1;
    for (int r = 12; r >= 0; --r) if (counts[r]) groups[n++] = {counts[r], r};
    if (n == 5) {
        if (groups[0].second - groups[4].second == 4) straight = groups[0].second;
        else if (counts[12] && counts[0] && counts[1] && counts[2] && counts[3]) straight = 3;
    }
    std::sort(groups.begin(), groups.end(), std::greater<std::pair<int, int>>());
    unsigned category = flush && straight >= 0 ? 8 : groups[0].first == 4 ? 7
        : groups[0].first == 3 && groups[1].first == 2 ? 6 : flush ? 5 : straight >= 0 ? 4
        : groups[0].first == 3 ? 3 : groups[0].first == 2 && groups[1].first == 2 ? 2
        : groups[0].first == 2 ? 1 : 0;
    uint32_t rank = category;
    for (int i = 0; i < 5; ++i)
        rank = rank * 15 + ((category == 8 || category == 4) ? (i == 0 ? straight + 1 : 0)
                                                                  : (i < n ? groups[i].second + 1 : 0));
    return rank;
}
void evaluator_oracle() {
    omp::HandEvaluator evaluator;
    std::map<uint16_t, uint32_t> ranks;
    std::array<uint64_t, 9> histogram{};
    uint64_t count = 0;
    for (int a = 0; a < 52; ++a) for (int b = 0; b < a; ++b) for (int c = 0; c < b; ++c)
        for (int d = 0; d < c; ++d) for (int e = 0; e < d; ++e) {
            const auto oracle = reference5({a,b,c,d,e});
            const auto value = evaluator.evaluate(omp::Hand::empty() + omp::Hand(a) + omp::Hand(b)
                + omp::Hand(c) + omp::Hand(d) + omp::Hand(e));
            const auto inserted = ranks.emplace(value, oracle);
            require(inserted.first->second == oracle, "five-card evaluator rank collision");
            ++histogram[oracle / (15*15*15*15*15)]; ++count;
        }
    require(count == 2598960 && ranks.size() == 7462, "five-card exhaustive cardinality");
    require(histogram == std::array<uint64_t, 9>{1302540,1098240,123552,54912,10200,5108,3744,624,40},
            "five-card category frequencies");
    uint32_t previous = 0;
    for (auto [rank, oracle] : ranks) { (void)rank; require(oracle > previous, "five-card ordering mismatch"); previous = oracle; }
    std::mt19937_64 random(90210);
    std::array<int, 52> deck{}; std::iota(deck.begin(), deck.end(), 0);
    for (int sample = 0; sample < 50000; ++sample) {
        omp::Hand h = omp::Hand::empty();
        for (int i = 0; i < 7; ++i) {
            std::swap(deck[i], deck[std::uniform_int_distribution<int>(i, 51)(random)]);
            h += omp::Hand(deck[i]);
        }
        uint32_t best = 0;
        for (int a = 0; a < 7; ++a) for (int b = a + 1; b < 7; ++b) {
            std::array<int, 5> five{}; int k = 0;
            for (int i = 0; i < 7; ++i) if (i != a && i != b) five[k++] = deck[i];
            best = std::max(best, reference5(five));
        }
        require(ranks.at(evaluator.evaluate(h)) == best, "seven-card best-of-21 mismatch");
    }
    std::cout << "Independent evaluator oracle: all 2,598,960 five-card hands and 50,000 seven-card hands passed.\n";
}
void minimizer_oracle() {
    for (auto mode : {aof2::Discounting::None, aof2::Discounting::DCFR, aof2::Discounting::HS}) {
        double p = .5, q = .5, pa = 0, pb = 0, qa = 0, qb = 0, ps = 0, qs = 0, norm = 0;
        for (uint64_t step = 1; step <= 300000; ++step) {
            const auto update = aof2::RegretUpdate::at(step, true, mode, 300000);
            // [[3,-1],[-2,2]] has exact row mix 1/2, column mix 3/8, value 1/2.
            update.apply(p, 8 * q - 3, pa, pb, ps);
            update.apply(q, 4 - 8 * p, qa, qb, qs);
            norm = update.average * norm + update.increment;
            const auto match = [](double a, double b) {
                a = std::max(0.0, a); b = std::max(0.0, b); return a+b > 0 ? a/(a+b) : .5;
            };
            p = match(pa, pb); q = match(qa, qb);
        }
        p = ps/norm; q = qs/norm;
        const double gap = std::max(4*q-1, 2-4*q) - std::min(5*p-2, 2-3*p);
        require(gap < .04 && std::abs(p-.5) < .015 && std::abs(q-.375) < .015,
                "regret optimizer fails exact asymmetric zero-sum equilibrium");
        std::cout << "Optimizer " << static_cast<int>(mode) << ": exact matrix gap=" << gap << '\n';
    }
}
int main() {
    try { evaluator_oracle(); minimizer_oracle(); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
