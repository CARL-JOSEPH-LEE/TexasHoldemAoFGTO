#include "ThreePlayerCfrSolver.h"
#include "ThreePlayerPayoff.h"
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

inline double rm_plus(double a, double b)
{
    const double s = a + b;
    if (s <= 0.0) return 0.5;
    return a / s;
}

}

ThreePlayerCfrSolver::ThreePlayerCfrSolver(const EquityTable& eq2,
                                            const ThreeWayEquityTable& eq3,
                                            const GameParams& params)
    : m_eq2(eq2), m_eq3(eq3), m_params(params)
{
    params.validate();
    const auto classes = all_hand_classes();
    for (int i = 0; i < NUM_HAND_CLASSES; ++i)
        m_combos[i] = classes[i].num_combos();
}

ThreePlayerStrategy ThreePlayerCfrSolver::solve(const Config& cfg)
{
    if (cfg.iterations == 0)
        throw std::invalid_argument("ThreePlayerCfrSolver::solve: iterations must be > 0");

    unsigned threads = cfg.threads ? cfg.threads : std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;

    ThreadPool pool((threads <= 1) ? 0u : threads);
    auto parallel_for = [&](int n, unsigned, auto&& body) {
        if (pool.empty()) {
            for (int k = 0; k < n; ++k) body(k);
            return;
        }
        pool.parallel_for(n, std::forward<decltype(body)>(body));
    };

    const double S = m_params.stack;
    const double sb_blind = m_params.sb_blind;
    const double bb_blind = m_params.bb_blind;

    const double POT_3WAY    = pot_3way(m_params);
    const double POT_BTN_BB  = pot_btn_bb(m_params);
    const double POT_BTN_SB  = pot_btn_sb(m_params);
    const double POT_SB_BB   = pot_sb_bb(m_params);

    const int N = NUM_HAND_CLASSES;

    std::vector<double> r_btn_push (N, 0.0), r_btn_fold(N, 0.0);
    std::vector<double> r_sbcp_call(N, 0.0), r_sbcp_fold(N, 0.0);
    std::vector<double> r_sbpf_push(N, 0.0), r_sbpf_fold(N, 0.0);
    std::vector<double> r_bb3_call (N, 0.0), r_bb3_fold (N, 0.0);
    std::vector<double> r_bbT_call (N, 0.0), r_bbT_fold (N, 0.0);
    std::vector<double> r_bbS_call (N, 0.0), r_bbS_fold (N, 0.0);

    std::vector<double> s_btn_push (N, 0.5);
    std::vector<double> s_sbcp_call(N, 0.5);
    std::vector<double> s_sbpf_push(N, 0.5);
    std::vector<double> s_bb3_call (N, 0.5);
    std::vector<double> s_bbT_call (N, 0.5);
    std::vector<double> s_bbS_call (N, 0.5);

    std::vector<long double> a_btn_push (N, 0.0L);
    std::vector<long double> a_sbcp_call(N, 0.0L);
    std::vector<long double> a_sbpf_push(N, 0.0L);
    std::vector<long double> a_bb3_call (N, 0.0L);
    std::vector<long double> a_bbT_call (N, 0.0L);
    std::vector<long double> a_bbS_call (N, 0.0L);
    long double weight_sum = 0.0L;

    const double* E2     = m_eq2.equity_matrix().data();
    const double* E3     = m_eq3.equity_array().data();
    const int64_t* F3    = m_eq3.feasibility_array().data();

    const auto t0 = std::chrono::steady_clock::now();

    auto inv_safe = [](long double w) -> long double {
        return (w > 0.0L) ? (1.0L / w) : 0.0L;
    };

    for (uint64_t t = 1; t <= cfg.iterations; ++t) {
        const double weight_t = static_cast<double>(t);

        parallel_for(N, threads, [&](int i) {
            long double cf_push = 0.0L;
            long double w_total = 0.0L;

            for (int j = 0; j < N; ++j) {
                const double pi_sbc = s_sbcp_call[j];
                const double sbc_call_const = POT_BTN_SB * E2[i * N + j] - S;
                const double sbc_inner_btnvssb = sbc_call_const;

                for (int k = 0; k < N; ++k) {
                    const int64_t f3 = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                    if (f3 == 0) continue;
                    const double f = static_cast<double>(f3);

                    const double pi_bb3 = s_bb3_call[k];
                    const double pi_bbT = s_bbT_call[k];

                    const double e3p0 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 0];

                    const double pay_3way    = POT_3WAY   * e3p0    - S;
                    const double pay_btn_bb  = POT_BTN_BB * E2[i * N + k] - S;

                    const double inner_push =
                          pi_sbc * (pi_bb3 * pay_3way + (1.0 - pi_bb3) * sbc_inner_btnvssb)
                        + (1.0 - pi_sbc) * (pi_bbT * pay_btn_bb + (1.0 - pi_bbT) * (sb_blind + bb_blind));

                    cf_push += f * inner_push;
                    w_total += f;
                }
            }

            if (w_total <= 0.0L) {
                a_btn_push[i] += static_cast<long double>(weight_t * s_btn_push[i]);
                return;
            }

            const double ev_push = static_cast<double>(cf_push * inv_safe(w_total));
            const double ev_fold = 0.0;
            const double cur = s_btn_push[i] * ev_push + (1.0 - s_btn_push[i]) * ev_fold;

            r_btn_push[i] = std::max(0.0, r_btn_push[i] + ev_push - cur);
            r_btn_fold[i] = std::max(0.0, r_btn_fold[i] + ev_fold - cur);
            s_btn_push[i] = rm_plus(r_btn_push[i], r_btn_fold[i]);
            a_btn_push[i] += static_cast<long double>(weight_t * s_btn_push[i]);
        });

        parallel_for(N, threads, [&](int j) {
            long double cf_call = 0.0L;
            long double cf_fold = 0.0L;
            long double w_total = 0.0L;

            for (int i = 0; i < N; ++i) {
                const double pi_btn = s_btn_push[i];
                if (pi_btn == 0.0) continue;
                const double btn_vs_sb_eq_for_sb = 1.0 - E2[i * N + j];
                const double pay_btn_vs_sb = POT_BTN_SB * btn_vs_sb_eq_for_sb - S;

                for (int k = 0; k < N; ++k) {
                    const int64_t f3 = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                    if (f3 == 0) continue;
                    const double f = static_cast<double>(f3);
                    const double w = f * pi_btn;

                    const double pi_bb3 = s_bb3_call[k];
                    const double e3p1 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 1];
                    const double pay_3way = POT_3WAY * e3p1 - S;

                    cf_call += w * (pi_bb3 * pay_3way + (1.0 - pi_bb3) * pay_btn_vs_sb);
                    cf_fold += w * (-sb_blind);
                    w_total += w;
                }
            }

            if (w_total <= 0.0L) {
                a_sbcp_call[j] += static_cast<long double>(weight_t * s_sbcp_call[j]);
                return;
            }
            const double ev_call = static_cast<double>(cf_call * inv_safe(w_total));
            const double ev_fold = static_cast<double>(cf_fold * inv_safe(w_total));
            const double cur = s_sbcp_call[j] * ev_call + (1.0 - s_sbcp_call[j]) * ev_fold;

            r_sbcp_call[j] = std::max(0.0, r_sbcp_call[j] + ev_call - cur);
            r_sbcp_fold[j] = std::max(0.0, r_sbcp_fold[j] + ev_fold - cur);
            s_sbcp_call[j] = rm_plus(r_sbcp_call[j], r_sbcp_fold[j]);
            a_sbcp_call[j] += static_cast<long double>(weight_t * s_sbcp_call[j]);
        });

        parallel_for(N, threads, [&](int j) {
            long double cf_push = 0.0L;
            long double cf_fold = 0.0L;
            long double w_total = 0.0L;

            for (int i = 0; i < N; ++i) {
                const double pi_btn_fold = 1.0 - s_btn_push[i];
                if (pi_btn_fold == 0.0) continue;
                for (int k = 0; k < N; ++k) {
                    const int64_t f3 = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                    if (f3 == 0) continue;
                    const double f = static_cast<double>(f3);
                    const double w = f * pi_btn_fold;

                    const double pi_bbS = s_bbS_call[k];
                    const double sb_eq_vs_bb = E2[j * N + k];
                    const double pay_sb_vs_bb = sb_vs_bb_called_ev(m_params, sb_eq_vs_bb);

                    cf_push += w * (pi_bbS * pay_sb_vs_bb + (1.0 - pi_bbS) * bb_blind);
                    cf_fold += w * (-sb_blind);
                    w_total += w;
                }
            }

            if (w_total <= 0.0L) {
                a_sbpf_push[j] += static_cast<long double>(weight_t * s_sbpf_push[j]);
                return;
            }
            const double ev_push = static_cast<double>(cf_push * inv_safe(w_total));
            const double ev_fold = static_cast<double>(cf_fold * inv_safe(w_total));
            const double cur = s_sbpf_push[j] * ev_push + (1.0 - s_sbpf_push[j]) * ev_fold;

            r_sbpf_push[j] = std::max(0.0, r_sbpf_push[j] + ev_push - cur);
            r_sbpf_fold[j] = std::max(0.0, r_sbpf_fold[j] + ev_fold - cur);
            s_sbpf_push[j] = rm_plus(r_sbpf_push[j], r_sbpf_fold[j]);
            a_sbpf_push[j] += static_cast<long double>(weight_t * s_sbpf_push[j]);
        });

        parallel_for(N, threads, [&](int k) {
            long double cf_call_3 = 0.0L, cf_fold_3 = 0.0L, w3 = 0.0L;
            long double cf_call_T = 0.0L, cf_fold_T = 0.0L, wT = 0.0L;
            long double cf_call_S = 0.0L, cf_fold_S = 0.0L, wS = 0.0L;

            for (int i = 0; i < N; ++i) {
                const double pi_btn = s_btn_push[i];
                const double pi_btn_fold = 1.0 - pi_btn;
                for (int j = 0; j < N; ++j) {
                    const int64_t f3 = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                    if (f3 == 0) continue;
                    const double f = static_cast<double>(f3);

                    const double pi_sbc = s_sbcp_call[j];
                    const double pi_sbp = s_sbpf_push[j];

                    if (pi_btn > 0.0) {
                        if (pi_sbc > 0.0) {
                            const double w = f * pi_btn * pi_sbc;
                            const double e3p2 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 2];
                            const double pay_3way = POT_3WAY * e3p2 - S;
                            cf_call_3 += w * pay_3way;
                            cf_fold_3 += w * (-bb_blind);
                            w3 += w;
                        }
                        if ((1.0 - pi_sbc) > 0.0) {
                            const double w = f * pi_btn * (1.0 - pi_sbc);
                            const double bb_eq = 1.0 - E2[i * N + k];
                            const double pay = POT_BTN_BB * bb_eq - S;
                            cf_call_T += w * pay;
                            cf_fold_T += w * (-bb_blind);
                            wT += w;
                        }
                    }
                    if (pi_btn_fold > 0.0 && pi_sbp > 0.0) {
                        const double w = f * pi_btn_fold * pi_sbp;
                        const double bb_eq = 1.0 - E2[j * N + k];
                        const double pay = POT_SB_BB * bb_eq - S;
                        cf_call_S += w * pay;
                        cf_fold_S += w * (-bb_blind);
                        wS += w;
                    }
                }
            }

            auto update_bb = [&](double& s, double& r_call, double& r_fold,
                                  std::vector<long double>& acc,
                                  long double cf_c, long double cf_f, long double w) {
                if (w <= 0.0L) {
                    acc[k] += static_cast<long double>(weight_t * s);
                    return;
                }
                const double ev_call = static_cast<double>(cf_c / w);
                const double ev_fold = static_cast<double>(cf_f / w);
                const double cur = s * ev_call + (1.0 - s) * ev_fold;
                r_call = std::max(0.0, r_call + ev_call - cur);
                r_fold = std::max(0.0, r_fold + ev_fold - cur);
                s = rm_plus(r_call, r_fold);
                acc[k] += static_cast<long double>(weight_t * s);
            };

            update_bb(s_bb3_call[k], r_bb3_call[k], r_bb3_fold[k], a_bb3_call, cf_call_3, cf_fold_3, w3);
            update_bb(s_bbT_call[k], r_bbT_call[k], r_bbT_fold[k], a_bbT_call, cf_call_T, cf_fold_T, wT);
            update_bb(s_bbS_call[k], r_bbS_call[k], r_bbS_fold[k], a_bbS_call, cf_call_S, cf_fold_S, wS);
        });

        weight_sum += static_cast<long double>(weight_t);

        if (cfg.on_progress && ((cfg.log_every && t % cfg.log_every == 0) || t == cfg.iterations)) {
            ThreePlayerStrategy snap;
            snap.params = m_params;
            for (int x = 0; x < N; ++x) {
                snap.btn_push[x]              = static_cast<double>(a_btn_push[x]  / weight_sum);
                snap.sb_call_vs_btn_push[x]   = static_cast<double>(a_sbcp_call[x] / weight_sum);
                snap.sb_push_vs_btn_fold[x]   = static_cast<double>(a_sbpf_push[x] / weight_sum);
                snap.bb_call_3way[x]          = static_cast<double>(a_bb3_call[x]  / weight_sum);
                snap.bb_call_vs_btn_only[x]   = static_cast<double>(a_bbT_call[x]  / weight_sum);
                snap.bb_call_vs_sb_only[x]    = static_cast<double>(a_bbS_call[x]  / weight_sum);
            }
            compute_evs_(snap.btn_push, snap.sb_call_vs_btn_push, snap.sb_push_vs_btn_fold,
                         snap.bb_call_3way, snap.bb_call_vs_btn_only, snap.bb_call_vs_sb_only,
                         snap, threads);
            CfrProgress3 p;
            p.iteration = t;
            p.btn_ev = snap.btn_ev;
            p.sb_ev  = snap.sb_ev;
            p.bb_ev  = snap.bb_ev;
            p.exploitability_bb = exploitability_(snap, threads);
            p.seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            cfg.on_progress(p);
        }
    }

    ThreePlayerStrategy out;
    out.params = m_params;
    for (int x = 0; x < N; ++x) {
        out.btn_push[x]              = static_cast<double>(a_btn_push[x]  / weight_sum);
        out.sb_call_vs_btn_push[x]   = static_cast<double>(a_sbcp_call[x] / weight_sum);
        out.sb_push_vs_btn_fold[x]   = static_cast<double>(a_sbpf_push[x] / weight_sum);
        out.bb_call_3way[x]          = static_cast<double>(a_bb3_call[x]  / weight_sum);
        out.bb_call_vs_btn_only[x]   = static_cast<double>(a_bbT_call[x]  / weight_sum);
        out.bb_call_vs_sb_only[x]    = static_cast<double>(a_bbS_call[x]  / weight_sum);
    }
    out.iterations = cfg.iterations;

    compute_evs_(out.btn_push, out.sb_call_vs_btn_push, out.sb_push_vs_btn_fold,
                 out.bb_call_3way, out.bb_call_vs_btn_only, out.bb_call_vs_sb_only,
                 out, threads);

    out.exploitability_bb = exploitability_(out, threads);
    return out;
}

void ThreePlayerCfrSolver::compute_evs_(
    const std::array<double, NUM_HAND_CLASSES>& btn_push,
    const std::array<double, NUM_HAND_CLASSES>& sb_call_vs_btn_push,
    const std::array<double, NUM_HAND_CLASSES>& sb_push_vs_btn_fold,
    const std::array<double, NUM_HAND_CLASSES>& bb_call_3way,
    const std::array<double, NUM_HAND_CLASSES>& bb_call_vs_btn_only,
    const std::array<double, NUM_HAND_CLASSES>& bb_call_vs_sb_only,
    ThreePlayerStrategy& out,
    unsigned threads) const
{
    const double S = m_params.stack;
    const double sb_blind = m_params.sb_blind;
    const double bb_blind = m_params.bb_blind;
    const double POT_3WAY    = pot_3way(m_params);
    const double POT_BTN_BB  = pot_btn_bb(m_params);
    const double POT_BTN_SB  = pot_btn_sb(m_params);
    const double POT_SB_BB   = pot_sb_bb(m_params);
    const int N = NUM_HAND_CLASSES;

    const double*  E2 = m_eq2.equity_matrix().data();
    const double*  E3 = m_eq3.equity_array().data();
    const int64_t* F3 = m_eq3.feasibility_array().data();

    out.btn_fold_ev = 0.0;
    out.sb_fold_ev_vs_btn_push = -sb_blind;
    out.sb_fold_ev_vs_btn_fold = -sb_blind;
    out.bb_fold_ev = -bb_blind;

    ThreadPool pool((threads <= 1) ? 0u : threads);
    auto parallel_for = [&](int n, unsigned, auto&& body) {
        if (pool.empty()) {
            for (int k = 0; k < n; ++k) body(k);
            return;
        }
        pool.parallel_for(n, std::forward<decltype(body)>(body));
    };

    parallel_for(N, threads, [&](int i) {
        long double cf = 0.0L, w = 0.0L;
        for (int j = 0; j < N; ++j) {
            const double pi_sbc = sb_call_vs_btn_push[j];
            for (int k = 0; k < N; ++k) {
                const int64_t f = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                if (f == 0) continue;
                const double pi_bb3 = bb_call_3way[k];
                const double pi_bbT = bb_call_vs_btn_only[k];
                const double e3p0 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 0];

                const double pay_3way   = POT_3WAY   * e3p0    - S;
                const double pay_btn_sb = POT_BTN_SB * E2[i * N + j] - S;
                const double pay_btn_bb = POT_BTN_BB * E2[i * N + k] - S;
                const double pay_blinds = sb_blind + bb_blind;

                const double inner =
                      pi_sbc * (pi_bb3 * pay_3way + (1.0 - pi_bb3) * pay_btn_sb)
                    + (1.0 - pi_sbc) * (pi_bbT * pay_btn_bb + (1.0 - pi_bbT) * pay_blinds);

                cf += static_cast<long double>(f) * inner;
                w  += static_cast<long double>(f);
            }
        }
        out.btn_push_ev[i] = (w > 0.0L) ? static_cast<double>(cf / w) : 0.0;
    });

    parallel_for(N, threads, [&](int j) {
        long double cf = 0.0L, w = 0.0L;
        for (int i = 0; i < N; ++i) {
            const double pi_btn = btn_push[i];
            if (pi_btn == 0.0) continue;
            const double pay_btn_vs_sb_for_sb = POT_BTN_SB * (1.0 - E2[i * N + j]) - S;
            for (int k = 0; k < N; ++k) {
                const int64_t f = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                if (f == 0) continue;
                const double pi_bb3 = bb_call_3way[k];
                const double e3p1 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 1];
                const double pay_3way = POT_3WAY * e3p1 - S;
                const double inner = pi_bb3 * pay_3way + (1.0 - pi_bb3) * pay_btn_vs_sb_for_sb;
                cf += static_cast<long double>(f) * pi_btn * inner;
                w  += static_cast<long double>(f) * pi_btn;
            }
        }
        out.sb_call_ev_vs_btn_push[j] = (w > 0.0L) ? static_cast<double>(cf / w) : 0.0;
    });

    parallel_for(N, threads, [&](int j) {
        long double cf = 0.0L, w = 0.0L;
        for (int i = 0; i < N; ++i) {
            const double pi_btn_fold = 1.0 - btn_push[i];
            if (pi_btn_fold == 0.0) continue;
            for (int k = 0; k < N; ++k) {
                const int64_t f = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                if (f == 0) continue;
                const double pi_bbS = bb_call_vs_sb_only[k];
                const double pay_sb_vs_bb = sb_vs_bb_called_ev(m_params, E2[j * N + k]);
                const double inner = pi_bbS * pay_sb_vs_bb + (1.0 - pi_bbS) * bb_blind;
                cf += static_cast<long double>(f) * pi_btn_fold * inner;
                w  += static_cast<long double>(f) * pi_btn_fold;
            }
        }
        out.sb_push_ev_vs_btn_fold[j] = (w > 0.0L) ? static_cast<double>(cf / w) : 0.0;
    });

    parallel_for(N, threads, [&](int k) {
        long double cf3 = 0.0L, w3 = 0.0L;
        long double cfT = 0.0L, wT = 0.0L;
        long double cfS = 0.0L, wS = 0.0L;
        for (int i = 0; i < N; ++i) {
            const double pi_btn = btn_push[i];
            const double pi_btn_fold = 1.0 - pi_btn;
            for (int j = 0; j < N; ++j) {
                const int64_t f = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                if (f == 0) continue;
                const double pi_sbc = sb_call_vs_btn_push[j];
                const double pi_sbp = sb_push_vs_btn_fold[j];
                if (pi_btn > 0.0 && pi_sbc > 0.0) {
                    const double e3p2 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 2];
                    const double pay = POT_3WAY * e3p2 - S;
                    const long double w = static_cast<long double>(f) * pi_btn * pi_sbc;
                    cf3 += w * pay;
                    w3  += w;
                }
                if (pi_btn > 0.0 && (1.0 - pi_sbc) > 0.0) {
                    const double pay = POT_BTN_BB * (1.0 - E2[i * N + k]) - S;
                    const long double w = static_cast<long double>(f) * pi_btn * (1.0 - pi_sbc);
                    cfT += w * pay;
                    wT  += w;
                }
                if (pi_btn_fold > 0.0 && pi_sbp > 0.0) {
                    const double pay = POT_SB_BB * (1.0 - E2[j * N + k]) - S;
                    const long double w = static_cast<long double>(f) * pi_btn_fold * pi_sbp;
                    cfS += w * pay;
                    wS  += w;
                }
            }
        }
        out.bb_call_ev_3way[k]        = (w3 > 0.0L) ? static_cast<double>(cf3 / w3) : 0.0;
        out.bb_call_ev_vs_btn_only[k] = (wT > 0.0L) ? static_cast<double>(cfT / wT) : 0.0;
        out.bb_call_ev_vs_sb_only[k]  = (wS > 0.0L) ? static_cast<double>(cfS / wS) : 0.0;
    });

    long double btn_total = 0.0L, w_player = 0.0L;
    for (int i = 0; i < N; ++i) {
        const double w = static_cast<double>(m_combos[i]);
        btn_total += w * (btn_push[i] * out.btn_push_ev[i] + (1.0 - btn_push[i]) * out.btn_fold_ev);
        w_player  += w;
    }
    out.btn_ev = static_cast<double>(btn_total / w_player);

    {
        long double sb_acc = 0.0L, sb_w = 0.0L;
        long double bb_acc = 0.0L, bb_w = 0.0L;
        for (int i = 0; i < N; ++i) {
            const double pi_btn = btn_push[i];
            for (int j = 0; j < N; ++j) {
                const double pi_sbc = sb_call_vs_btn_push[j];
                const double pi_sbp = sb_push_vs_btn_fold[j];
                for (int k = 0; k < N; ++k) {
                    const int64_t f = F3[(static_cast<int64_t>(i) * N + j) * N + k];
                    if (f == 0) continue;
                    const double pi_bb3 = bb_call_3way[k];
                    const double pi_bbT = bb_call_vs_btn_only[k];
                    const double pi_bbS = bb_call_vs_sb_only[k];

                    const double e3p1 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 1];
                    const double e3p2 = E3[3 * (((static_cast<int64_t>(i) * N + j) * N) + k) + 2];

                    const double pay_3w_sb = POT_3WAY   * e3p1 - S;
                    const double pay_3w_bb = POT_3WAY   * e3p2 - S;
                    const double pay_btn_sb_for_sb = POT_BTN_SB * (1.0 - E2[i * N + j]) - S;
                    const double pay_btn_bb_for_bb = POT_BTN_BB * (1.0 - E2[i * N + k]) - S;
                    const double pay_sb_bb_for_sb  = sb_vs_bb_called_ev(m_params, E2[j * N + k]);
                    const double pay_sb_bb_for_bb  = bb_vs_sb_called_ev(m_params, E2[j * N + k]);

                    const double prob_btnP_sbC_bbC = pi_btn * pi_sbc * pi_bb3;
                    const double prob_btnP_sbC_bbF = pi_btn * pi_sbc * (1.0 - pi_bb3);
                    const double prob_btnP_sbF_bbC = pi_btn * (1.0 - pi_sbc) * pi_bbT;
                    const double prob_btnP_sbF_bbF = pi_btn * (1.0 - pi_sbc) * (1.0 - pi_bbT);
                    const double prob_btnF_sbP_bbC = (1.0 - pi_btn) * pi_sbp * pi_bbS;
                    const double prob_btnF_sbP_bbF = (1.0 - pi_btn) * pi_sbp * (1.0 - pi_bbS);
                    const double prob_btnF_sbF     = (1.0 - pi_btn) * (1.0 - pi_sbp);

                    const double sb_payoff =
                          prob_btnP_sbC_bbC * pay_3w_sb
                        + prob_btnP_sbC_bbF * pay_btn_sb_for_sb
                        + prob_btnP_sbF_bbC * (-sb_blind)
                        + prob_btnP_sbF_bbF * (-sb_blind)
                        + prob_btnF_sbP_bbC * pay_sb_bb_for_sb
                        + prob_btnF_sbP_bbF * (bb_blind)
                        + prob_btnF_sbF     * (-sb_blind);

                    const double bb_payoff =
                          prob_btnP_sbC_bbC * pay_3w_bb
                        + prob_btnP_sbC_bbF * (-bb_blind)
                        + prob_btnP_sbF_bbC * pay_btn_bb_for_bb
                        + prob_btnP_sbF_bbF * (-bb_blind)
                        + prob_btnF_sbP_bbC * pay_sb_bb_for_bb
                        + prob_btnF_sbP_bbF * (-bb_blind)
                        + prob_btnF_sbF     * (sb_blind);

                    sb_acc += static_cast<long double>(f) * sb_payoff;
                    sb_w   += static_cast<long double>(f);
                    bb_acc += static_cast<long double>(f) * bb_payoff;
                    bb_w   += static_cast<long double>(f);
                }
            }
        }
        out.sb_ev = static_cast<double>(sb_acc / sb_w);
        out.bb_ev = static_cast<double>(bb_acc / bb_w);
    }
}

double ThreePlayerCfrSolver::exploitability_(const ThreePlayerStrategy& s, unsigned /* threads */) const
{
    const int N = NUM_HAND_CLASSES;

    auto pure_max_gain = [&](const std::array<double, NUM_HAND_CLASSES>& freq,
                              const std::array<double, NUM_HAND_CLASSES>& act_ev,
                              double fold_ev) -> double
    {
        long double total_w = 0.0L, gain = 0.0L;
        for (int x = 0; x < N; ++x) {
            const double w = static_cast<double>(m_combos[x]);
            const double cur = freq[x] * act_ev[x] + (1.0 - freq[x]) * fold_ev;
            const double br  = std::max(act_ev[x], fold_ev);
            gain    += static_cast<long double>(w) * (br - cur);
            total_w += static_cast<long double>(w);
        }
        return static_cast<double>(gain / total_w);
    };

    const double btn_gain = pure_max_gain(s.btn_push, s.btn_push_ev, s.btn_fold_ev);
    const double sbc_gain = pure_max_gain(s.sb_call_vs_btn_push, s.sb_call_ev_vs_btn_push, s.sb_fold_ev_vs_btn_push);
    const double sbp_gain = pure_max_gain(s.sb_push_vs_btn_fold, s.sb_push_ev_vs_btn_fold, s.sb_fold_ev_vs_btn_fold);
    const double bb3_gain = pure_max_gain(s.bb_call_3way,        s.bb_call_ev_3way,        s.bb_fold_ev);
    const double bbT_gain = pure_max_gain(s.bb_call_vs_btn_only, s.bb_call_ev_vs_btn_only, s.bb_fold_ev);
    const double bbS_gain = pure_max_gain(s.bb_call_vs_sb_only,  s.bb_call_ev_vs_sb_only,  s.bb_fold_ev);

    return std::max(0.0, btn_gain) + std::max(0.0, sbc_gain) + std::max(0.0, sbp_gain)
         + std::max(0.0, bb3_gain) + std::max(0.0, bbT_gain) + std::max(0.0, bbS_gain);
}

}
