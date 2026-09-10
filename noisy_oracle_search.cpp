// noisy_oracle_search.cpp
//
// Companion to number_guessing_game.cpp and knuth_optimal_bst.cpp in this
// repo.
//
// Both of those assume a truthful yes/no channel. This file asks what
// happens when it isn't: each answer is wrong with some fixed probability
// errorRate, independently every time. This is the probabilistic form of
// the Rényi-Ulam "searching with a liar" question.
//
// The clean theoretical answer is the capacity of a binary symmetric
// channel: log2(N) / (1 - H(errorRate)) queries, where H is binary
// entropy -- same Shannon machinery as shannonEntropyBits() in
// number_guessing_game.cpp, just applied to channel noise instead of a
// skewed key-frequency prior.
//
// THIS FILE DOES NOT HIT THAT BOUND, and the gap is real, not a rounding
// error: measured at N=1000, errorRate=0.1, this converges consistently
// but needs a median of ~120 queries against a bound of ~19. The
// algorithm here -- always query the weighted median of the current
// posterior, update via Bayes' rule -- is the probabilistic bisection
// algorithm (Horstein, 1963). It's provably consistent (converges to the
// true value with probability 1) but not bandwidth-optimal. Reaching the
// channel-capacity bound needs something closer to an error-correcting
// code spread across queries (Karp & Kleinberg, 2007), not a greedy
// per-query rule. runNoisyOracleComparison in number_guessing_game.cpp
// checks this gap empirically every run instead of asserting a bound
// that isn't actually met.
//
// Build:
//   g++ -O2 -std=c++17 -Wall -Wextra noisy_oracle_search.cpp -o noisy_search
//   ./noisy_search

#ifndef OPTIMAL_SEARCH_STRATEGIES_NOISY_ORACLE_SEARCH_CPP
#define OPTIMAL_SEARCH_STRATEGIES_NOISY_ORACLE_SEARCH_CPP

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

// Randomness is injected (randReal01), not owned by this file -- same
// separation knuth_optimal_bst.cpp keeps from the rng()/Monte Carlo loop
// that drives it in number_guessing_game.cpp. Keeps the query-selection
// and belief-update logic deterministic given its inputs, and testable
// without a real RNG (see the top-2 bundling regression test in
// tests.cpp, which hands this a fixed belief vector directly).

double binaryEntropyBits(double p)
{
    if (p <= 0.0 || p >= 1.0) return 0.0;
    return -p * std::log2(p) - (1 - p) * std::log2(1 - p);
}

// Channel-capacity lower bound on expected queries -- the bound this
// file's algorithm does not reach. See file header.
double noisyChannelCapacityBound(long long rangeSize, double errorRate)
{
    if (errorRate <= 0.0 || errorRate >= 0.5)
        return std::ceil(std::log2(static_cast<double>(rangeSize)));
    return std::log2(static_cast<double>(rangeSize)) / (1.0 - binaryEntropyBits(errorRate));
}

// Picks the next query boundary given the current posterior over
// [lo, hi). Defaults to the weighted median -- the single most
// informative query in isolation -- but overrides to split the top-2
// candidates directly once one of them has pulled ahead of a uniform
// prior.
//
// BUG THIS OVERRIDE FIXES: without it, if the two most likely candidates
// land on the same side of the median cut, every later query multiplies
// both of their belief by the identical factor -- their ratio is then
// frozen forever (two values scaled by the same factor keep the same
// ratio), so the search can converge on the wrong candidate with
// arbitrarily high confidence and never recover. Reproduced with a
// two-cluster belief vector during testing; see
// test_top2_bundling_regression in tests.cpp.
//
// The 4x-uniform threshold below is load-bearing, not a tuning nicety:
// applying this override from the first query, before any real
// concentration has happened, degrades the search into scanning adjacent
// indices one at a time (every early "top-2" pair is just a noise-level
// near-tie). That was the first version of this fix and it was worse
// than no fix.
long long nextNoisyQuery(const std::vector<double>& belief, long long lo, long long hi)
{
    double total = 0.0;
    for (long long i = lo; i < hi; ++i) total += belief[i];

    double half = total / 2.0;
    double running = 0.0;
    long long medianCut = hi;
    for (long long i = lo; i < hi; ++i)
    {
        running += belief[i];
        if (running >= half) { medianCut = std::min(i + 1, hi); break; }
    }

    long long top1 = lo, top2 = lo;
    double top1Val = -1.0, top2Val = -1.0;
    for (long long i = lo; i < hi; ++i)
    {
        if (belief[i] > top1Val)
        {
            top2 = top1; top2Val = top1Val;
            top1 = i; top1Val = belief[i];
        }
        else if (belief[i] > top2Val)
        {
            top2 = i; top2Val = belief[i];
        }
    }

    // BUG THIS FIXES (found via testing, at errorRate=0 -- see
    // test_noisy_search_degenerates_to_binary_search_at_zero_error_rate in
    // tests.cpp): this threshold used to compare top1Val against
    // 1.0/(hi-lo), where (hi-lo) is the FULL original range passed in
    // every call, never shrunk -- the belief array only zeroes out dead
    // candidates, it never shrinks. As live candidates get eliminated, each
    // survivor's share grows just from having fewer rivals, with no real
    // concentration in the intended sense, so that comparison eventually
    // exceeds the stale full-range baseline on a perfectly UNIFORM residual
    // cluster -- triggering the override anyway and degrading the search
    // into an adjacent-index scan, the same failure mode the original
    // 4x-uniform guard was meant to prevent, just reached a different way.
    // Fixed by basing "uniform" on the count of currently-live candidates
    // (nonzero belief) instead of the original range size.
    long long liveCount = 0;
    for (long long i = lo; i < hi; ++i) if (belief[i] > 0.0) ++liveCount;
    double uniform = (liveCount > 0) ? (total / static_cast<double>(liveCount)) : (total / (hi - lo));
    if (top1 != top2 && top1Val > 4.0 * uniform)
    {
        long long rivalLo = std::min(top1, top2);
        long long rivalHi = std::max(top1, top2);
        bool sameSide = (rivalLo < medianCut) == (rivalHi < medianCut);
        if (sameSide && rivalLo + 1 <= rivalHi) return rivalLo + 1;
    }

    return medianCut;
}

struct NoisySearchOutcome
{
    long long guess;
    long long attempts;
    double confidence;
};

// Drives nextNoisyQuery + a Bayes update in a loop until a candidate's
// posterior crosses confidenceThreshold or maxQueries is hit. truth is
// computed directly (secretIdx < q) since that part needs no injected
// randomness; randReal01 supplies only the oracle's lie coin.
NoisySearchOutcome noisySearchAttempts(long long secretIdx, long long lo, long long hi,
                                        double errorRate, double confidenceThreshold,
                                        long long maxQueries,
                                        const std::function<double()>& randReal01)
{
    std::vector<double> belief(hi + 1, 0.0);
    double uniform = 1.0 / static_cast<double>(hi - lo + 1);
    for (long long i = lo; i <= hi; ++i) belief[i] = uniform;

    long long attempts = 0;
    while (attempts < maxQueries)
    {
        auto bestIt = std::max_element(belief.begin() + lo, belief.begin() + hi + 1);
        if (*bestIt >= confidenceThreshold)
            return {static_cast<long long>(bestIt - belief.begin()), attempts, *bestIt};

        long long q = nextNoisyQuery(belief, lo, hi + 1);
        if (q <= lo || q > hi) break;

        bool truth = secretIdx < q;
        bool lie = randReal01() < errorRate;
        bool answerLt = lie ? !truth : truth;
        ++attempts;

        double norm = 0.0;
        for (long long i = lo; i <= hi; ++i)
        {
            bool consistent = (i < q) == answerLt;
            belief[i] *= consistent ? (1.0 - errorRate) : errorRate;
            norm += belief[i];
        }
        for (long long i = lo; i <= hi; ++i) belief[i] /= norm;
    }

    auto bestIt = std::max_element(belief.begin() + lo, belief.begin() + hi + 1);
    return {static_cast<long long>(bestIt - belief.begin()), attempts, *bestIt};
}

#ifndef NOISY_ORACLE_NO_MAIN
#include <random>
int main()
{
    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto randReal01 = [&]() { return unit(gen); };

    long long n = 1000;
    double errorRate = 0.1;
    std::uniform_int_distribution<long long> secretDist(0, n - 1);
    long long secretIdx = secretDist(gen);

    auto outcome = noisySearchAttempts(secretIdx, 0, n - 1, errorRate, 0.99, 5000, randReal01);

    std::cout << "=== Noisy Oracle Search ===\n";
    std::cout << "N=" << n << ", oracle error rate=" << errorRate << "\n";
    std::cout << "Found " << outcome.guess << " in " << outcome.attempts
              << " queries (confidence " << outcome.confidence << "), actual secret was "
              << secretIdx << "\n";
    std::cout << "Channel-capacity bound (not met by this algorithm): "
              << noisyChannelCapacityBound(n, errorRate) << "\n";
    return 0;
}
#endif

#endif // OPTIMAL_SEARCH_STRATEGIES_NOISY_ORACLE_SEARCH_CPP
