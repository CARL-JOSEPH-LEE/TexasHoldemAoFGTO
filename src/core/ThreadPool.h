#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace aof2 {

class ThreadPool
{
public:
    explicit ThreadPool(unsigned n_workers)
    {
        if (n_workers <= 1) return;
        m_workers.reserve(n_workers);
        for (unsigned i = 0; i < n_workers; ++i)
            m_workers.emplace_back([this] { worker_loop(); });
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_stop = true;
            ++m_epoch;
        }
        m_cv_work.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool empty() const { return m_workers.empty(); }
    unsigned size() const { return static_cast<unsigned>(m_workers.size()); }

    template <class F>
    void parallel_for(int n, F&& body)
    {
        if (n <= 0) return;
        if (m_workers.empty()) {
            for (int k = 0; k < n; ++k) body(k);
            return;
        }

        std::function<void(int)> fn = std::forward<F>(body);
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_fn = std::move(fn);
            m_n = n;
            m_next.store(0, std::memory_order_relaxed);
            m_done = 0;
            ++m_epoch;
        }
        m_cv_work.notify_all();

        std::unique_lock<std::mutex> lk(m_mu);
        m_cv_done.wait(lk, [&] { return m_done == m_workers.size(); });
        m_fn = nullptr;
    }

private:
    void worker_loop()
    {
        uint64_t my_epoch = 0;
        for (;;) {
            std::function<void(int)> fn;
            int n;
            {
                std::unique_lock<std::mutex> lk(m_mu);
                m_cv_work.wait(lk, [&] { return m_stop || m_epoch > my_epoch; });
                if (m_stop) return;
                my_epoch = m_epoch;
                fn = m_fn;
                n = m_n;
            }

            if (fn && n > 0) {
                for (;;) {
                    int k = m_next.fetch_add(1, std::memory_order_relaxed);
                    if (k >= n) break;
                    fn(k);
                }
            }

            {
                std::lock_guard<std::mutex> lk(m_mu);
                ++m_done;
                if (m_done == m_workers.size())
                    m_cv_done.notify_one();
            }
        }
    }

    std::vector<std::thread> m_workers;
    std::mutex m_mu;
    std::condition_variable m_cv_work;
    std::condition_variable m_cv_done;
    bool m_stop = false;
    uint64_t m_epoch = 0;

    std::function<void(int)> m_fn;
    int m_n = 0;
    std::atomic<int> m_next{0};
    unsigned m_done = 0;
};

}
