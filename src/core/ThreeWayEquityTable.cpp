#include "ThreeWayEquityTable.h"

#include <omp/EquityCalculator.h>
#include <omp/CardRange.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace aof2 {

static const char EQ3W_MAGIC[8]    = {'A','O','F','2','E','Q','3','W'};
static const uint32_t EQ3W_VERSION = 1;

namespace {

inline bool overlap(const Combo& a, const Combo& b) noexcept
{
    return a.card_high == b.card_high || a.card_high == b.card_low
        || a.card_low  == b.card_high || a.card_low  == b.card_low;
}

int64_t count_feasible_3way(const std::vector<Combo>& a,
                            const std::vector<Combo>& b,
                            const std::vector<Combo>& c)
{
    int64_t n = 0;
    for (const auto& x : a)
        for (const auto& y : b) {
            if (overlap(x, y)) continue;
            for (const auto& z : c) {
                if (overlap(x, z)) continue;
                if (overlap(y, z)) continue;
                ++n;
            }
        }
    return n;
}

void compute_cell(const std::string& r0, const std::string& r1, const std::string& r2,
                  double& e0, double& e1, double& e2, int64_t& feas)
{
    omp::EquityCalculator eq;
    std::vector<omp::CardRange> ranges{
        omp::CardRange(r0), omp::CardRange(r1), omp::CardRange(r2)
    };
    constexpr unsigned INNER_THREADS = 1;
    if (!eq.start(ranges, 0, 0, true, 0.0, nullptr, 0.2, INNER_THREADS)) {
        e0 = e1 = e2 = 1.0 / 3.0;
        feas = 0;
        return;
    }
    eq.wait();
    auto r = eq.getResults();
    if (!r.finished)
        throw std::runtime_error("3way EquityCalculator did not finish");
    if (r.hands == 0) {
        e0 = e1 = e2 = 1.0 / 3.0;
        feas = 0;
        return;
    }
    e0 = r.equity[0];
    e1 = r.equity[1];
    e2 = r.equity[2];
    feas = static_cast<int64_t>(r.evaluatedPreflopCombos);
    if (feas == 0) feas = 1;
}

}

ThreeWayEquityTable::ThreeWayEquityTable()
    : m_eq(static_cast<size_t>(NUM_HAND_CLASSES) * NUM_HAND_CLASSES * NUM_HAND_CLASSES * 3, 0.0),
      m_feas(static_cast<size_t>(NUM_HAND_CLASSES) * NUM_HAND_CLASSES * NUM_HAND_CLASSES, 0)
{
}

void ThreeWayEquityTable::compute(unsigned threads, bool verbose)
{
    const auto classes = all_hand_classes();
    std::vector<std::vector<Combo>> combos(NUM_HAND_CLASSES);
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        combos[i] = classes[i].enumerate_combos();

    if (verbose) std::fprintf(stderr, "[eq3w] computing combinatorial feasibility (169^3)...\n");
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        for (int j = 0; j < NUM_HAND_CLASSES; ++j)
            for (int k = 0; k < NUM_HAND_CLASSES; ++k)
                m_feas[flat_index(i, j, k)] = count_feasible_3way(combos[i], combos[j], combos[k]);

    struct Job { int i; int j; int k; };
    std::vector<Job> jobs;
    jobs.reserve(static_cast<size_t>(NUM_HAND_CLASSES) * NUM_HAND_CLASSES * (NUM_HAND_CLASSES + 2) / 6);
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        for (int j = i; j < NUM_HAND_CLASSES; ++j)
            for (int k = j; k < NUM_HAND_CLASSES; ++k)
                jobs.push_back({i, j, k});

    if (threads == 0) threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;

    if (verbose) std::fprintf(stderr,
        "[eq3w] %zu canonical (i<=j<=k) cells, %u threads\n",
        jobs.size(), threads);

    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    std::mutex log_mu;
    const auto t0 = std::chrono::steady_clock::now();

    auto worker = [&]() {
        while (true) {
            const size_t idx = next.fetch_add(1);
            if (idx >= jobs.size()) break;
            const Job& job = jobs[idx];

            const std::string r0 = classes[job.i].to_omp_range();
            const std::string r1 = classes[job.j].to_omp_range();
            const std::string r2 = classes[job.k].to_omp_range();

            double eqs[3] = {0, 0, 0};
            int64_t feas = 0;

            const int64_t global_feas = m_feas[flat_index(job.i, job.j, job.k)];
            if (global_feas == 0) {
                eqs[0] = eqs[1] = eqs[2] = 1.0 / 3.0;
            } else {
                compute_cell(r0, r1, r2, eqs[0], eqs[1], eqs[2], feas);
            }

            const int idxs[3] = {job.i, job.j, job.k};
            const int perms[6][3] = {
                {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
            };
            for (int p = 0; p < 6; ++p) {
                const int a = idxs[perms[p][0]];
                const int b = idxs[perms[p][1]];
                const int c = idxs[perms[p][2]];
                const int64_t f = flat_index(a, b, c);
                m_eq[3 * f + 0] = eqs[perms[p][0]];
                m_eq[3 * f + 1] = eqs[perms[p][1]];
                m_eq[3 * f + 2] = eqs[perms[p][2]];
            }

            const size_t d = done.fetch_add(1) + 1;
            if (verbose && (d % 200 == 0 || d == jobs.size())) {
                std::lock_guard<std::mutex> lk(log_mu);
                const auto now = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(now - t0).count();
                const double rate = d / std::max(secs, 1e-9);
                const double eta = (jobs.size() - d) / std::max(rate, 1e-9);
                std::fprintf(stderr,
                    "[eq3w] %zu / %zu  (%.2f%%, %.0fs, %.2f cells/s, ETA %.0fs)\n",
                    d, jobs.size(), 100.0 * d / jobs.size(),
                    secs, rate, eta);
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    m_computed = true;
}

void ThreeWayEquityTable::save(const std::string& path) const
{
    if (!m_computed)
        throw std::runtime_error("ThreeWayEquityTable::save: not computed");

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("ThreeWayEquityTable::save: cannot open " + path);

    f.write(EQ3W_MAGIC, 8);
    f.write(reinterpret_cast<const char*>(&EQ3W_VERSION), sizeof(EQ3W_VERSION));
    uint32_t n = NUM_HAND_CLASSES;
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(m_eq.data()),   sizeof(double)  * m_eq.size());
    f.write(reinterpret_cast<const char*>(m_feas.data()), sizeof(int64_t) * m_feas.size());

    if (!f) throw std::runtime_error("ThreeWayEquityTable::save: write failure");
}

void ThreeWayEquityTable::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("ThreeWayEquityTable::load: cannot open " + path);

    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, EQ3W_MAGIC, 8) != 0)
        throw std::runtime_error("ThreeWayEquityTable::load: bad magic");

    uint32_t version = 0, n = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (version != EQ3W_VERSION)
        throw std::runtime_error("ThreeWayEquityTable::load: version mismatch");
    if (n != NUM_HAND_CLASSES)
        throw std::runtime_error("ThreeWayEquityTable::load: hand class count mismatch");

    f.read(reinterpret_cast<char*>(m_eq.data()),   sizeof(double)  * m_eq.size());
    f.read(reinterpret_cast<char*>(m_feas.data()), sizeof(int64_t) * m_feas.size());

    if (!f) throw std::runtime_error("ThreeWayEquityTable::load: read truncated");

    m_computed = true;
}

ThreeWayEquityTable ThreeWayEquityTable::load_from(const std::string& path)
{
    ThreeWayEquityTable t;
    t.load(path);
    return t;
}

}
