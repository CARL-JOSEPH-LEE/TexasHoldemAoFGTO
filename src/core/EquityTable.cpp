#include "EquityTable.h"

#include <omp/EquityCalculator.h>
#include <omp/CardRange.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace aof2 {

static const char EQUITY_MAGIC[8] = {'A','O','F','2','E','Q','T','1'};
static const uint32_t EQUITY_VERSION = 1;

namespace {

int64_t count_feasible(const std::vector<Combo>& a, const std::vector<Combo>& b)
{
    int64_t n = 0;
    for (const auto& x : a) {
        for (const auto& y : b) {
            if (x.card_high == y.card_high) continue;
            if (x.card_high == y.card_low)  continue;
            if (x.card_low  == y.card_high) continue;
            if (x.card_low  == y.card_low)  continue;
            ++n;
        }
    }
    return n;
}

double exact_pair_equity(const std::string& sb_range, const std::string& bb_range)
{
    omp::EquityCalculator eq;
    std::vector<omp::CardRange> ranges{omp::CardRange(sb_range), omp::CardRange(bb_range)};
    constexpr unsigned INNER_THREADS = 1;
    if (!eq.start(ranges, 0, 0, true, 0.0, nullptr, 0.2, INNER_THREADS)) {
        throw std::runtime_error("EquityCalculator::start failed for " + sb_range + " vs " + bb_range);
    }
    eq.wait();
    auto r = eq.getResults();
    if (!r.finished)
        throw std::runtime_error("EquityCalculator did not finish for " + sb_range + " vs " + bb_range);
    if (r.hands == 0)
        throw std::runtime_error("EquityCalculator returned zero hands for " + sb_range + " vs " + bb_range);
    return r.equity[0];
}

}

EquityTable::EquityTable()
    : m_equity(NUM_HAND_CLASSES * NUM_HAND_CLASSES, 0.0),
      m_feasible(NUM_HAND_CLASSES * NUM_HAND_CLASSES, 0)
{
}

void EquityTable::compute(unsigned threads, bool verbose)
{
    const auto classes = all_hand_classes();

    std::vector<std::vector<Combo>> combos(NUM_HAND_CLASSES);
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        combos[i] = classes[i].enumerate_combos();

    for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
        for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
            m_feasible[i * NUM_HAND_CLASSES + j] = count_feasible(combos[i], combos[j]);
        }
    }

    struct Job { int i; int j; };
    std::vector<Job> jobs;
    jobs.reserve(NUM_HAND_CLASSES * (NUM_HAND_CLASSES + 1) / 2);
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        for (int j = i; j < NUM_HAND_CLASSES; ++j)
            jobs.push_back({i, j});

    if (threads == 0) threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;

    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    std::mutex log_mu;
    const auto t0 = std::chrono::steady_clock::now();

    auto worker = [&]() {
        while (true) {
            const size_t k = next.fetch_add(1);
            if (k >= jobs.size()) break;
            const Job& job = jobs[k];

            const std::string sb_r = classes[job.i].to_omp_range();
            const std::string bb_r = classes[job.j].to_omp_range();

            const int64_t feas = m_feasible[job.i * NUM_HAND_CLASSES + job.j];
            double eq;
            if (feas == 0) {
                eq = 0.5;
            } else if (job.i == job.j) {
                eq = 0.5;
            } else {
                eq = exact_pair_equity(sb_r, bb_r);
            }

            m_equity[job.i * NUM_HAND_CLASSES + job.j] = eq;
            m_equity[job.j * NUM_HAND_CLASSES + job.i] = 1.0 - eq;

            const size_t d = done.fetch_add(1) + 1;
            if (verbose && (d % 50 == 0 || d == jobs.size())) {
                std::lock_guard<std::mutex> lk(log_mu);
                const auto now = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(now - t0).count();
                std::fprintf(stderr,
                    "[equity] %zu / %zu  (%.1f%%, %.1fs, %.2f cells/s)\n",
                    d, jobs.size(), 100.0 * d / jobs.size(),
                    secs, d / std::max(secs, 1e-9));
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    m_computed = true;
}

void EquityTable::save(const std::string& path) const
{
    if (!m_computed)
        throw std::runtime_error("EquityTable::save: table not computed");

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("EquityTable::save: cannot open " + path);

    f.write(EQUITY_MAGIC, 8);
    f.write(reinterpret_cast<const char*>(&EQUITY_VERSION), sizeof(EQUITY_VERSION));
    uint32_t n = NUM_HAND_CLASSES;
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(m_equity.data()),
            sizeof(double) * m_equity.size());
    f.write(reinterpret_cast<const char*>(m_feasible.data()),
            sizeof(int64_t) * m_feasible.size());

    if (!f) throw std::runtime_error("EquityTable::save: write failure");
}

void EquityTable::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("EquityTable::load: cannot open " + path);

    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, EQUITY_MAGIC, 8) != 0)
        throw std::runtime_error("EquityTable::load: bad magic in " + path);

    uint32_t version = 0, n = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (version != EQUITY_VERSION)
        throw std::runtime_error("EquityTable::load: version mismatch");
    if (n != NUM_HAND_CLASSES)
        throw std::runtime_error("EquityTable::load: size mismatch");

    f.read(reinterpret_cast<char*>(m_equity.data()),
           sizeof(double) * m_equity.size());
    f.read(reinterpret_cast<char*>(m_feasible.data()),
           sizeof(int64_t) * m_feasible.size());

    if (!f) throw std::runtime_error("EquityTable::load: read truncated");

    m_computed = true;
}

EquityTable EquityTable::load_from(const std::string& path)
{
    EquityTable t;
    t.load(path);
    return t;
}

int64_t EquityTable::total_feasible_pairs() const
{
    int64_t s = 0;
    for (auto v : m_feasible) s += v;
    return s;
}

double EquityTable::row_total_weight(int sb_class) const
{
    double s = 0.0;
    for (int j = 0; j < NUM_HAND_CLASSES; ++j)
        s += static_cast<double>(m_feasible[sb_class * NUM_HAND_CLASSES + j]);
    return s;
}

}
