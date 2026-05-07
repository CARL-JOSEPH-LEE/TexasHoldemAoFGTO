#include "core/HandClass.h"

#include <omp/EquityCalculator.h>
#include <omp/CardRange.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    int n_samples = 200;
    unsigned threads = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--samples" && i + 1 < argc) n_samples = std::atoi(argv[++i]);
        else if (a == "--threads" && i + 1 < argc) threads = static_cast<unsigned>(std::atoi(argv[++i]));
    }
    if (threads == 0) threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;

    using namespace aof2;
    const auto classes = all_hand_classes();
    const int64_t total_canonical = static_cast<int64_t>(NUM_HAND_CLASSES) * NUM_HAND_CLASSES * (NUM_HAND_CLASSES + 2) / 6;

    std::mt19937_64 rng(0xC0FFEE);
    std::uniform_int_distribution<int> dist(0, NUM_HAND_CLASSES - 1);

    struct Job { int i; int j; int k; };
    std::vector<Job> jobs;
    jobs.reserve(n_samples);
    while (static_cast<int>(jobs.size()) < n_samples) {
        int a = dist(rng), b = dist(rng), c = dist(rng);
        int x = std::min({a, b, c});
        int z = std::max({a, b, c});
        int y = a + b + c - x - z;
        jobs.push_back({x, y, z});
    }

    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    std::atomic<long long> total_micros{0};
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

            omp::EquityCalculator eq;
            std::vector<omp::CardRange> ranges{
                omp::CardRange(r0), omp::CardRange(r1), omp::CardRange(r2)
            };

            const auto t_a = std::chrono::steady_clock::now();
            if (!eq.start(ranges, 0, 0, true, 0.0, nullptr, 0.2, 1)) {
                ++done;
                continue;
            }
            eq.wait();
            auto r = eq.getResults();
            const auto t_b = std::chrono::steady_clock::now();
            const long long us = std::chrono::duration_cast<std::chrono::microseconds>(t_b - t_a).count();
            total_micros += us;

            const size_t d = ++done;
            if (d % 20 == 0) {
                std::lock_guard<std::mutex> lk(log_mu);
                const auto now = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(now - t0).count();
                std::fprintf(stderr,
                    "[bench3w] %zu/%zu  wall=%.1fs  per-cell-cpu(us)=%.0f  classes=(%s, %s, %s)  hands=%llu\n",
                    d, jobs.size(), secs,
                    static_cast<double>(total_micros.load()) / d,
                    classes[job.i].to_string().c_str(),
                    classes[job.j].to_string().c_str(),
                    classes[job.k].to_string().c_str(),
                    static_cast<unsigned long long>(r.hands));
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    const auto t1 = std::chrono::steady_clock::now();
    const double wall = std::chrono::duration<double>(t1 - t0).count();
    const double per_cell_wall = wall / n_samples;
    const double full_table_eta_s = per_cell_wall * static_cast<double>(total_canonical);

    std::printf("\n[bench3w] threads = %u\n", threads);
    std::printf("[bench3w] sampled cells = %d\n", n_samples);
    std::printf("[bench3w] total canonical (i<=j<=k) cells = %lld\n",
        static_cast<long long>(total_canonical));
    std::printf("[bench3w] wall time      = %.2fs (%.3fs / cell, multi-threaded)\n", wall, per_cell_wall);
    std::printf("[bench3w] avg single-thread cell time = %.0f us\n",
        static_cast<double>(total_micros.load()) / n_samples);
    std::printf("[bench3w] ETA full 3-way table at %u threads: %.1f s = %.1f min = %.2f h\n",
        threads, full_table_eta_s, full_table_eta_s / 60.0, full_table_eta_s / 3600.0);
    return 0;
}
