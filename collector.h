// Copyright (C) 2016 by telfer - MIT License. See LICENSE.txt

#ifndef _EXP_PERF_COLLECTOR_H
#define _EXP_PERF_COLLECTOR_H

#include "counter.h"

#include <cmath>

namespace exp_perf
{
//
// A collector that implements the collection and testing algorithm in [Add
// reference]
//
// Defaults to PERF_COUNT_HW_INSTRUCTIONS, but falls back to
// PERF_COUNT_SW_TASK_CLOCK on systems that don't support HW counters
//
class collector
{
  public:
    // Constructor.
    //
    // See [add reference] for details.
    // First, know that the MLE estimator of the shift L of a shifted
    // exponential E = l exp(-l(x-L)) is the sample minimum L_hat = min(X_i).
    //
    // alpha - the level of confidence in the sample size estimate.
    //
    // alpha bounds the probability that we did see a number less than L + e in
    // n trials, which is:
    //
    //   P = exp(-l e n)
    //
    // bounded. The collect algorithm works by iteratively finding n so that we
    // get the fewest number of samples necessary to get the accuracy we desire.
    //
    // beta_min - Since e = beta_min * L, i.e. beta_min is the percentage of L
    // that we allow error to accumulate.
    //
    // min_incr - Each time we estimate n, we take small steps to get
    // there. This is the min step increment we take to get there.
    //
    // max_incr - The maximum step size we take toward n.
    //
    // max_rounds - The maximum number of times we try to hit beta < beta_min
    // before giving up.
    //
    // n_init - First value of n to estimate.
    collector(double alpha,
              double beta_min,
              int min_incr,
              int max_incr,
              int max_rounds,
              int n_init);

    // The main user entry point.
    //
    // init_input_sz - the starting value of the input size
    // num_run - the number of runs to make, each time doubling init_input_sz
    // start - run once before run(), not timed
    // stop - run once after run(), not timed
    // run - the function under test
    // u - an updater that receives the results of the algorithm
    template <typename t_start,
              typename t_stop,
              typename t_run,
              typename t_updater>
    void collect(int init_input_sz,
                 int num_runs,
                 t_start start,
                 t_stop stop,
                 t_run run,
                 t_updater u);

  private:
    // We use these to keep track of where the PERF counters are.
    enum {
        PERF_SW_TASK_CLK   = 0,
        PERF_SW_CPU_CLK    = 1,
        PERF_HW_INSTR_CTR  = 2,
        PERF_HW_CYCLES_CLK = 3
    };

    // Wraps the calls to start, stop and run around the internal counter.
    template <typename t_start, typename t_stop, typename t_run>
    const exp_perf::counter::counts_t& get_counts(int sample_sz,
                                              t_start start,
                                              t_stop stop,
                                              t_run run);

    // Most of the algorithm implemented here, for each input size.
    template <typename t_start,
              typename t_stop,
              typename t_run,
              typename t_updater>
    void collect_for_input_size(int input_sz,
                                t_start start,
                                t_stop stop,
                                t_run run,
                                t_updater u);

    int m_ctr_idx;  // The index of the counter we are using
    const double m_alpha;
    const double m_beta_min;
    const int m_min_incr;
    const int m_max_incr;
    const int m_max_rounds;
    const int m_n_init;
    counter m_counter;
};

inline collector::collector(double alpha,
                            double beta_min,
                            int min_incr,
                            int max_incr,
                            int max_rounds,
                            int n_init)
    : m_ctr_idx(-1)
    , m_alpha(alpha)
    , m_beta_min(beta_min)
    , m_min_incr(min_incr)
    , m_max_incr(max_incr)
    , m_max_rounds(max_rounds)
    , m_n_init(n_init)
    , m_counter({PERF_COUNT_SW_TASK_CLOCK, PERF_COUNT_SW_CPU_CLOCK},
                {PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_REF_CPU_CYCLES})

{
    m_ctr_idx = PERF_SW_TASK_CLK;
    if (m_counter.get_status(PERF_HW_INSTR_CTR)) {
        m_ctr_idx = PERF_HW_INSTR_CTR;
    }
}

template <typename t_start, typename t_stop, typename t_run, typename t_updater>
void
collector::collect(int N,
                   int num_runs,
                   t_start start,
                   t_stop stop,
                   t_run run,
                   t_updater u)
{
    for (int i = 0; i < num_runs; ++i, N *= 2) {
        collect_for_input_size(N, start, stop, run, u);
    }
}

template <typename t_start, typename t_stop, typename t_run>
const exp_perf::counter::counts_t&
collector::get_counts(int N, t_start start, t_stop stop, t_run run)
{
    start(N);
    m_counter.start();
    run(N);
    m_counter.stop();
    stop(N);
    return m_counter.get_counts();
}

template <typename t_start, typename t_stop, typename t_run, typename t_updater>
void
collector::collect_for_input_size(int input_sz,
                                  t_start start,
                                  t_stop stop,
                                  t_run run,
                                  t_updater u)
{
    int n           = m_n_init;
    double sum      = 0;
    long long L_hat = 0;
    int n_tot       = 0;
    double beta     = m_beta_min + 1;
    double xbar     = 0;
    double lam_hat  = 0;
    double fac      = 0;
    for (int i = 0; i < m_max_rounds; ++i) {
        // Gather counts based on the current value of n
        for (int i = 0; i <= n; ++i) {
            long long cnt = get_counts(input_sz, start, stop, run)[m_ctr_idx];
            sum += cnt;
            if (i == 0 || cnt < L_hat) {
                L_hat = cnt;
            }
        }

        // Calculate the beta estimate
        n_tot   = n_tot + n;
        xbar    = sum / n_tot;
        lam_hat = 1. / (xbar - L_hat);
        fac     = n_tot * lam_hat * L_hat;
        beta    = -std::log(m_alpha) / fac;
        if (beta <= m_beta_min) {
            break;
        }

        // To get the new N, estimate what n should be based off the beta we
        // calculated and use this as a guide to see how many more we should
        // gather.
        fac       = lam_hat * m_beta_min * L_hat;
        int new_n = (int)(-std::log(m_alpha) / fac);
        if (new_n < n_tot) {
            // Our forumlas don't work anymore, gather min_incr until
            // something good happens, or we exceed the loop_cnt
            n = m_min_incr;
        } else {
            // Only gather a clamped number of samples.
            n = new_n - n_tot;
            if (n > m_max_incr) {
                n = m_max_incr;
            } else if (n < m_min_incr) {
                n = m_min_incr;
            }
        }
    }
    u(input_sz, sum, L_hat, n_tot);
}
}

#endif  // _EXP_PERF_COLLECTOR_H
