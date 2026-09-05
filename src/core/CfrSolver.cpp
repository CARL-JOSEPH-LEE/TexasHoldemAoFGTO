#include "CfrSolver.h"
#include "ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace aof2 {

namespace {

inline double regret_match_plus(double r_a, double r_b)
{
    const double sum = r_a + r_b;
    if (sum <= 0.0) return 0.5;
    return r_a / sum;
}

}

CfrSolver::CfrSolver(const EquityTable& eq, const GameParams& params)
    : m_eq(eq), m_params(params)
{
    params.validate();
    const auto classes = all_hand_classes();
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        m_combos[i] = classes[i].num_combos();
}

double CfrSolver::sb_ev(const std::array<double, NUM_HAND_CLASSES>& sb_push,
                        const std::array<double, NUM_HAND_CLASSES>& bb_call) const
{
    const double S = m_params.stack;
    const double sb_blind = m_params.sb_blind;
    const double bb_blind = m_params.bb_blind;

    long double total_w = 0.0L;
    long double total_ev = 0.0L;

    for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
        const double wi = static_cast<double>(m_combos[i]);
        long double ev_push_num = 0.0L;
        long double F_total_i  = 0.0L;

        for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double called_ev = (2.0 * E - 1.0) * S;
            const double folded_ev = bb_blind;
            const double cell_ev = bb_call[j] * called_ev + (1.0 - bb_call[j]) * folded_ev;
            ev_push_num += F * cell_ev;
            F_total_i  += F;
        }

        const double ev_push = (F_total_i > 0.0L) ? static_cast<double>(ev_push_num / F_total_i) : 0.0;
        const double ev_fold = -sb_blind;
        const double ev_i = sb_push[i] * ev_push + (1.0 - sb_push[i]) * ev_fold;

        total_ev += static_cast<long double>(wi) * static_cast<long double>(ev_i);
        total_w  += static_cast<long double>(wi);
    }

    return static_cast<double>(total_ev / total_w);
}

double CfrSolver::exploitability(
    const std::array<double, NUM_HAND_CLASSES>& sb_push,
    const std::array<double, NUM_HAND_CLASSES>& bb_call) const
{
    const double S = m_params.stack;
    const double sb_blind = m_params.sb_blind;
    const double bb_blind = m_params.bb_blind;

    std::array<double, NUM_HAND_CLASSES> sb_br = sb_push;
    std::array<double, NUM_HAND_CLASSES> bb_br = bb_call;

    for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
        long double ev_push_num = 0.0L;
        long double F_total_i  = 0.0L;
        for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double called_ev = (2.0 * E - 1.0) * S;
            const double folded_ev = bb_blind;
            const double cell_ev = bb_call[j] * called_ev + (1.0 - bb_call[j]) * folded_ev;
            ev_push_num += F * cell_ev;
            F_total_i  += F;
        }
        const double ev_push = (F_total_i > 0.0L) ? static_cast<double>(ev_push_num / F_total_i) : 0.0;
        const double ev_fold = -sb_blind;
        sb_br[i] = (ev_push > ev_fold) ? 1.0 : (ev_push < ev_fold ? 0.0 : sb_push[i]);
    }

    long double sb_orig_ev_num = 0.0L, sb_orig_w = 0.0L;
    long double sb_br_ev_num   = 0.0L;

    for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
        const double wi = static_cast<double>(m_combos[i]);
        long double ev_push_num = 0.0L, F_total_i = 0.0L;
        for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double called_ev = (2.0 * E - 1.0) * S;
            const double folded_ev = bb_blind;
            const double cell_ev = bb_call[j] * called_ev + (1.0 - bb_call[j]) * folded_ev;
            ev_push_num += F * cell_ev;
            F_total_i  += F;
        }
        const double ev_push = (F_total_i > 0.0L) ? static_cast<double>(ev_push_num / F_total_i) : 0.0;
        const double ev_fold = -sb_blind;
        sb_orig_ev_num += wi * (sb_push[i] * ev_push + (1.0 - sb_push[i]) * ev_fold);
        sb_br_ev_num   += wi * (sb_br[i]   * ev_push + (1.0 - sb_br[i])   * ev_fold);
        sb_orig_w      += wi;
    }
    const double sb_gain = static_cast<double>((sb_br_ev_num - sb_orig_ev_num) / sb_orig_w);

    for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
        long double ev_call_num = 0.0L, w_total_j = 0.0L;
        for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double bb_called_ev = (1.0 - 2.0 * E) * S;
            const double r = sb_push[i] * F;
            ev_call_num += r * bb_called_ev;
            w_total_j   += r;
        }
        const double ev_call = (w_total_j > 0.0L) ? static_cast<double>(ev_call_num / w_total_j) : 0.0;
        const double ev_fold = -bb_blind;
        bb_br[j] = (ev_call > ev_fold) ? 1.0 : (ev_call < ev_fold ? 0.0 : bb_call[j]);
    }

    long double bb_orig_ev_num = 0.0L, bb_w = 0.0L;
    long double bb_br_ev_num   = 0.0L;

    for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
        const double wj = static_cast<double>(m_combos[j]);

        long double sb_pushed_w_at_j = 0.0L;
        long double sb_total_w_at_j = 0.0L;
        long double ev_call_pushed_num = 0.0L;

        for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double bb_called_ev = (1.0 - 2.0 * E) * S;
            ev_call_pushed_num += F * sb_push[i] * bb_called_ev;
            sb_pushed_w_at_j   += F * sb_push[i];
            sb_total_w_at_j    += F;
        }

        if (sb_total_w_at_j == 0.0L) continue;

        const double p_sb_push_given_j = static_cast<double>(sb_pushed_w_at_j / sb_total_w_at_j);

        long double ev_call_in_pushed = (sb_pushed_w_at_j > 0.0L)
            ? (ev_call_pushed_num / sb_pushed_w_at_j) : 0.0L;

        const double ev_when_sb_folds = sb_blind;

        const double bb_orig_action_ev = bb_call[j] * static_cast<double>(ev_call_in_pushed)
                                       + (1.0 - bb_call[j]) * (-bb_blind);
        const double bb_br_action_ev   = bb_br[j]   * static_cast<double>(ev_call_in_pushed)
                                       + (1.0 - bb_br[j])   * (-bb_blind);

        const double bb_orig_total = p_sb_push_given_j * bb_orig_action_ev
                                   + (1.0 - p_sb_push_given_j) * ev_when_sb_folds;
        const double bb_br_total   = p_sb_push_given_j * bb_br_action_ev
                                   + (1.0 - p_sb_push_given_j) * ev_when_sb_folds;

        bb_orig_ev_num += wj * bb_orig_total;
        bb_br_ev_num   += wj * bb_br_total;
        bb_w           += wj;
    }
    const double bb_gain = static_cast<double>((bb_br_ev_num - bb_orig_ev_num) / bb_w);

    return std::max(0.0, sb_gain) + std::max(0.0, bb_gain);
}

CfrSolver::HandEvBreakdown CfrSolver::breakdown(
    const std::array<double, NUM_HAND_CLASSES>& sb_push,
    const std::array<double, NUM_HAND_CLASSES>& bb_call) const
{
    HandEvBreakdown out{};
    const double S = m_params.stack;
    const double sb_blind = m_params.sb_blind;
    const double bb_blind = m_params.bb_blind;
    out.sb_fold_ev = -sb_blind;
    out.bb_fold_ev = -bb_blind;

    for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
        long double ev_push_num = 0.0L;
        long double F_total_i  = 0.0L;
        for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double called_ev = (2.0 * E - 1.0) * S;
            const double folded_ev = bb_blind;
            const double cell_ev = bb_call[j] * called_ev + (1.0 - bb_call[j]) * folded_ev;
            ev_push_num += F * cell_ev;
            F_total_i  += F;
        }
        out.sb_push_ev[i] = (F_total_i > 0.0L) ? static_cast<double>(ev_push_num / F_total_i) : 0.0;
    }

    for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
        long double ev_call_num = 0.0L;
        long double w_total = 0.0L;
        for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
            const double F = static_cast<double>(m_eq.feasible_combos(i, j));
            if (F == 0.0) continue;
            const double r = F * sb_push[i];
            if (r == 0.0) continue;
            const double E = m_eq.equity(i, j);
            const double bb_called_ev = (1.0 - 2.0 * E) * S;
            ev_call_num += r * bb_called_ev;
            w_total     += r;
        }
        out.bb_call_ev[j] = (w_total > 0.0L) ? static_cast<double>(ev_call_num / w_total) : 0.0;
    }

    return out;
}

Strategy CfrSolver::solve(const Config& cfg)
{
    if (cfg.iterations == 0)
        throw std::invalid_argument("CfrSolver::solve: iterations must be > 0");

    unsigned threads = cfg.threads ? cfg.threads : std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;

    const double S = m_params.stack;
    const double sb_blind = m_params.sb_blind;
    const double bb_blind = m_params.bb_blind;

    std::vector<double> EV_called(NUM_HAND_CLASSES * NUM_HAND_CLASSES);
    std::vector<double> F(NUM_HAND_CLASSES * NUM_HAND_CLASSES);
    std::vector<double> F_total_i(NUM_HAND_CLASSES, 0.0);
    std::vector<double> F_total_j(NUM_HAND_CLASSES, 0.0);

    for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
        for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
            const double f = static_cast<double>(m_eq.feasible_combos(i, j));
            const double e = m_eq.equity(i, j);
            EV_called[i * NUM_HAND_CLASSES + j] = (2.0 * e - 1.0) * S;
            F[i * NUM_HAND_CLASSES + j] = f;
            F_total_i[i] += f;
            F_total_j[j] += f;
        }
    }

    std::vector<double> regret_sb_push(NUM_HAND_CLASSES, 0.0);
    std::vector<double> regret_sb_fold(NUM_HAND_CLASSES, 0.0);
    std::vector<double> regret_bb_call(NUM_HAND_CLASSES, 0.0);
    std::vector<double> regret_bb_fold(NUM_HAND_CLASSES, 0.0);

    std::vector<double> sigma_sb_push(NUM_HAND_CLASSES, 0.5);
    std::vector<double> sigma_bb_call(NUM_HAND_CLASSES, 0.5);

    std::vector<long double> avg_sb_push(NUM_HAND_CLASSES, 0.0L);
    std::vector<long double> avg_bb_call(NUM_HAND_CLASSES, 0.0L);
    long double avg_weight_sum = 0.0L;

    const auto t0 = std::chrono::steady_clock::now();

    const unsigned worker_threads = (threads <= 1) ? 0u : threads;
    ThreadPool pool(worker_threads);

    auto parallel_for = [&](int n, auto&& body) {
        if (pool.empty()) {
            for (int k = 0; k < n; ++k) body(k);
            return;
        }
        pool.parallel_for(n, std::forward<decltype(body)>(body));
    };

    for (uint64_t t = 1; t <= cfg.iterations; ++t) {
        const double weight_t = static_cast<double>(t);

        parallel_for(NUM_HAND_CLASSES, [&](int i) {
            long double ev_push_num = 0.0L;
            const double Fi = F_total_i[i];
            if (Fi == 0.0) return;

            const double* EV_row = &EV_called[i * NUM_HAND_CLASSES];
            const double* F_row  = &F[i * NUM_HAND_CLASSES];
            for (int j = 0; j < NUM_HAND_CLASSES; ++j) {
                const double f = F_row[j];
                if (f == 0.0) continue;
                const double cell_ev = sigma_bb_call[j] * EV_row[j]
                                     + (1.0 - sigma_bb_call[j]) * bb_blind;
                ev_push_num += f * cell_ev;
            }
            const double ev_push = static_cast<double>(ev_push_num) / Fi;
            const double ev_fold = -sb_blind;
            const double cur = sigma_sb_push[i] * ev_push + (1.0 - sigma_sb_push[i]) * ev_fold;

            regret_sb_push[i] = std::max(0.0, regret_sb_push[i] + ev_push - cur);
            regret_sb_fold[i] = std::max(0.0, regret_sb_fold[i] + ev_fold - cur);
            sigma_sb_push[i] = regret_match_plus(regret_sb_push[i], regret_sb_fold[i]);
            avg_sb_push[i] += static_cast<long double>(weight_t * sigma_sb_push[i]);
        });

        parallel_for(NUM_HAND_CLASSES, [&](int j) {
            long double ev_call_num = 0.0L;
            long double w_total = 0.0L;

            for (int i = 0; i < NUM_HAND_CLASSES; ++i) {
                const double f = F[i * NUM_HAND_CLASSES + j];
                if (f == 0.0) continue;
                const double r = f * sigma_sb_push[i];
                if (r == 0.0) continue;
                ev_call_num += r * (-EV_called[i * NUM_HAND_CLASSES + j]);
                w_total     += r;
            }

            if (w_total <= 0.0L) {
                avg_bb_call[j] += static_cast<long double>(weight_t * sigma_bb_call[j]);
                return;
            }
            const double ev_call = static_cast<double>(ev_call_num / w_total);
            const double ev_fold = -bb_blind;
            const double cur = sigma_bb_call[j] * ev_call + (1.0 - sigma_bb_call[j]) * ev_fold;

            regret_bb_call[j] = std::max(0.0, regret_bb_call[j] + ev_call - cur);
            regret_bb_fold[j] = std::max(0.0, regret_bb_fold[j] + ev_fold - cur);
            sigma_bb_call[j] = regret_match_plus(regret_bb_call[j], regret_bb_fold[j]);
            avg_bb_call[j] += static_cast<long double>(weight_t * sigma_bb_call[j]);
        });

        avg_weight_sum += static_cast<long double>(weight_t);

        if (cfg.on_progress && ((cfg.log_every && t % cfg.log_every == 0) || t == cfg.iterations)) {
            std::array<double, NUM_HAND_CLASSES> avg_sb{}, avg_bb{};
            for (int k = 0; k < NUM_HAND_CLASSES; ++k) {
                avg_sb[k] = static_cast<double>(avg_sb_push[k] / avg_weight_sum);
                avg_bb[k] = static_cast<double>(avg_bb_call[k] / avg_weight_sum);
            }
            CfrProgress p;
            p.iteration = t;
            p.sb_ev = sb_ev(avg_sb, avg_bb);
            p.exploitability_bb = exploitability(avg_sb, avg_bb);
            p.seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            cfg.on_progress(p);
        }
    }

    Strategy out;
    out.params = m_params;
    for (int k = 0; k < NUM_HAND_CLASSES; ++k) {
        out.sb_push[k] = static_cast<double>(avg_sb_push[k] / avg_weight_sum);
        out.bb_call[k] = static_cast<double>(avg_bb_call[k] / avg_weight_sum);
    }
    out.iterations = cfg.iterations;
    out.sb_ev = sb_ev(out.sb_push, out.bb_call);
    out.exploitability_bb = exploitability(out.sb_push, out.bb_call);

    auto bd = breakdown(out.sb_push, out.bb_call);
    for (int k = 0; k < NUM_HAND_CLASSES; ++k) {
        out.sb_push_ev[k] = bd.sb_push_ev[k];
        out.bb_call_ev[k] = bd.bb_call_ev[k];
    }
    out.sb_fold_ev = bd.sb_fold_ev;
    out.bb_fold_ev = bd.bb_fold_ev;

    return out;
}

}
