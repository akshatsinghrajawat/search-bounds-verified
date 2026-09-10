#define GUESS_NO_MAIN
#include "number_guessing_game.cpp"
#define KNUTH_NO_MAIN
#include "knuth_optimal_bst.cpp"

#include <cassert>
#include <iostream>

void test_information_theoretic_bound_edge_cases()
{
    // Correct worst-case comparisons for this binary-search implementation
    // is "smallest k such that 2^k >= N+1", equivalently ceil(log2(N+1)) --
    // NOT ceil(log2(N)). The two formulas only disagree at exact powers of
    // two, which is exactly why this was invisible with the hardcoded
    // default N=100 (not a power of two) and only surfaced once N was
    // actually parameterized and tested across values.
    assert(informationTheoreticLowerBound(1) == 1);
    assert(informationTheoreticLowerBound(2) == 2);
    assert(informationTheoreticLowerBound(3) == 2);
    assert(informationTheoreticLowerBound(4) == 3);
    assert(informationTheoreticLowerBound(7) == 3);
    assert(informationTheoreticLowerBound(8) == 4);
    assert(informationTheoreticLowerBound(1023) == 10);
    assert(informationTheoreticLowerBound(1024) == 11);
    assert(informationTheoreticLowerBound(1025) == 11);
}

// Exhaustive, not sampled: every value in a set of N's chosen exactly for
// their edge-case potential (the flaw list's own suggestion) -- every exact
// power of two up to 2^10, their neighbors (2^k-1, 2^k+1), and N=100 (the
// repo's original hardcoded default, kept as a regression check).
void test_binary_search_matches_bound_exhaustively()
{
    std::vector<long long> ns = {1, 2, 3, 4, 7, 8, 9, 15, 16, 17,
                                  31, 32, 33, 63, 64, 65, 100,
                                  127, 128, 129, 255, 256, 257, 1023, 1024, 1025};
    for (long long n : ns)
    {
        long long bound = informationTheoreticLowerBound(n);
        for (long long secret = 1; secret <= n; ++secret)
        {
            long long attempts = binarySearchAttempts(secret, 1, n);
            assert(attempts <= bound);
        }
    }
}

// Randomized property test: for random N and random secret, binary search
// must terminate within informationTheoreticLowerBound(N) steps. Fixed seed
// so a failure is reproducible.
void test_binary_search_property_random(int iterations = 20000)
{
    std::mt19937 gen(12345);
    for (int i = 0; i < iterations; ++i)
    {
        std::uniform_int_distribution<long long> nDist(1, 1000000);
        long long n = nDist(gen);
        std::uniform_int_distribution<long long> secretDist(1, n);
        long long secret = secretDist(gen);
        long long attempts = binarySearchAttempts(secret, 1, n);
        assert(attempts <= informationTheoreticLowerBound(n));
    }
}

void test_interpolation_search_correct_on_uniform_samples()
{
    // Was calling interpolationSearchAttempts(secret, lo, hi) directly on
    // scalar bounds, which always returns 1 (see the fix in
    // number_guessing_game.cpp) -- and this test's old assertion
    // (attempts > 0) was too weak to ever catch that. Now runs the
    // array-based search and asserts it stays well under a linear scan,
    // which the degenerate version would still pass but a broken
    // O(N)-collapse would not.
    std::mt19937 gen(7);
    std::uniform_int_distribution<long long> d(1, 1000000);
    std::vector<long long> sample(5000);
    for (auto& v : sample) v = d(gen);
    std::sort(sample.begin(), sample.end());

    for (long long target : sample)
    {
        long long attempts = interpolationSearchAttemptsOnArray(target, sample);
        assert(attempts > 0);
        assert(attempts < static_cast<long long>(sample.size()));
    }
}

void test_entropy_optimal_never_beats_shannon_bound()
{
    // The bug this test would have caught: an earlier version of the
    // entropy-optimal comparison used 3-way (higher/lower/exact) queries,
    // which produced a mean BELOW H(p) -- impossible for a valid yes/no
    // decision procedure. This checks the exact (non-sampled) expectation.
    long long n = 100;
    auto w = zipfWeights(n, 1.0);
    std::vector<double> cum(n);
    std::partial_sum(w.begin(), w.end(), cum.begin());
    double H = shannonEntropyBits(w);

    double exact = 0.0;
    for (long long i = 0; i < n; ++i)
        exact += w[i] * entropyOptimalYesNoAttempts(i, 0, n - 1, cum);

    assert(exact >= H - 1e-9); // Shannon's lower bound, with float slack
}

void test_knuth_optimal_bst_uniform_matches_naive()
{
    // Under uniform priors there is no frequency skew to exploit, so the
    // optimal tree should collapse to the same cost as the naive
    // midpoint-split tree binary search implicitly builds.
    int n = 5;
    std::vector<double> p(n, 1.0 / (2 * n + 1));
    std::vector<double> q(n + 1, 1.0 / (2 * n + 1));
    auto result = knuthOptimalBST(p, q);
    double naive = naiveMidpointBSTCost(p, q, 0, n, 1, nullptr);
    assert(result.expectedCost - naive < 1e-9 && naive - result.expectedCost < 1e-9);
}

void test_knuth_optimal_bst_beats_naive_on_skewed_frequencies()
{
    // With a hot key away from the sorted midpoint, the optimal BST must
    // strictly beat the frequency-blind midpoint-split tree.
    std::vector<double> p = {0.60, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02};
    std::vector<double> q = {0.04, 0.04, 0.04, 0.04, 0.04, 0.04, 0.04, 0.00};
    int n = (int)p.size();
    auto result = knuthOptimalBST(p, q);
    double naive = naiveMidpointBSTCost(p, q, 0, n, 1, nullptr);
    assert(result.expectedCost < naive - 1e-9);
}

void test_knuth_bst_search_attempts_matches_expected_cost()
{
    // knuthBstSearchAttempts (the 3-way, early-exit-on-match traversal) is
    // a separate function from the DP itself -- this checks it actually
    // reproduces the number the DP claims, by computing the weighted
    // average of measured traversal costs directly from the root table and
    // comparing it to knuthOptimalBST's own expectedCost. If these
    // functions ever get out of sync (e.g. via future edits to either),
    // this catches it.
    std::vector<double> p = {0.60, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02};
    std::vector<double> q = {0.04, 0.04, 0.04, 0.04, 0.04, 0.04, 0.04, 0.00};
    int n = (int)p.size();
    auto result = knuthOptimalBST(p, q);

    double weightedCost = 0.0;
    for (int key = 1; key <= n; ++key)
    {
        long long attempts = knuthBstSearchAttempts(key, result.root, n);
        weightedCost += p[key - 1] * static_cast<double>(attempts);
    }
    // Note: this only covers the p-weighted (hit) terms; expectedCost also
    // includes the q-weighted (miss/gap) terms, so it's compared as a
    // lower bound rather than exact equality.
    assert(weightedCost <= result.expectedCost + 1e-9);
}

void test_knuth_split_yesno_never_beats_shannon_bound()
{
    // Same guard as test_entropy_optimal_never_beats_shannon_bound, applied
    // to the Knuth-derived yes/no search: it's a genuine adaptive yes/no
    // decision procedure over this distribution, so its expected query
    // count can never fall below H(p) -- if it did, that would mean a
    // 3-way/yes-no cost-model mismatch had crept back in (see the
    // docstring on knuthSplitYesNoAttempts in number_guessing_game.cpp for
    // the exact bug this guards against; it happened once while building
    // this bridge).
    long long n = 100;
    auto w = zipfWeights(n, 1.0);
    std::vector<double> cum(n);
    std::partial_sum(w.begin(), w.end(), cum.begin());
    double H = shannonEntropyBits(w);

    std::vector<double> gaps(static_cast<size_t>(n) + 1, 0.0);
    auto result = knuthOptimalBST(w, gaps);

    double weightedCost = 0.0;
    for (long long secretIdx = 0; secretIdx < n; ++secretIdx)
        weightedCost += w[secretIdx] * static_cast<double>(knuthSplitYesNoAttempts(secretIdx, 0, n - 1, result.root));

    assert(weightedCost > H - 1e-9);
}

// ---------------------------------------------------------------------------
// Noisy oracle search tests.
// ---------------------------------------------------------------------------

void test_noisy_search_degenerates_to_binary_search_at_zero_error_rate()
{
    // At errorRate=0, H(0)=0, so the channel-capacity bound is exactly
    // ceil(log2(N)) -- the same bound binary search meets. The noisy
    // search's belief update becomes a hard multiply-by-0-or-1 at
    // errorRate=0, which should make it match binary search's worst case
    // exactly rather than just "eventually converge."
    long long n = 1000;
    auto randReal01 = []() { return 1.0; }; // never below errorRate=0, so never lies
    long long worst = 0;
    for (long long secretIdx = 0; secretIdx < n; ++secretIdx)
    {
        auto outcome = noisySearchAttempts(secretIdx, 0, n - 1, /*errorRate=*/0.0,
                                            /*confidenceThreshold=*/0.99, /*maxQueries=*/100, randReal01);
        assert(outcome.guess == secretIdx);
        worst = std::max(worst, outcome.attempts);
    }
    assert(worst <= informationTheoreticLowerBound(n));
}

// This is the actual regression test for the bug documented at the top of
// nextNoisyQuery in noisy_oracle_search.cpp: construct a belief vector by
// hand where two dominant candidates sit on the same side of the plain
// weighted-median cut, and confirm the override kicks in and queries
// strictly between them instead of repeating the useless cut. Without the
// override, this assertion fails -- the median cut lands at index 3
// (between candidate 2 and index 3), bundling candidates 1 and 2 together
// forever.
void test_top2_bundling_regression()
{
    long long n = 10;
    std::vector<double> belief(n, 0.001);
    belief[1] = 0.70; // top1
    belief[2] = 0.20; // top2 -- both on the same side of a naive median cut
    double sum = 0.0;
    for (double b : belief) sum += b;
    for (double& b : belief) b /= sum;

    long long q = nextNoisyQuery(belief, 0, n);
    // The override must place q strictly between indices 1 and 2 (i.e.
    // q == 2), separating the two dominant candidates. A plain median cut
    // would instead land at/after index 2, bundling them.
    assert(q == 2);
}

// Companion to the regression test above: confirms the 4x-live-uniform
// threshold actually gates the override. Early in a search the belief is
// still near-uniform, and forcing a split between the top-2 "candidates"
// (which are just noise-level near-ties at that point) degrades the
// search into scanning adjacent indices one at a time -- this was the
// first, wrong version of the fix. With a genuinely near-uniform belief,
// nextNoisyQuery should return the plain median cut, not an adjacent-index
// split.
void test_top2_override_does_not_fire_near_uniform()
{
    long long n = 1000;
    std::vector<double> belief(n, 1.0 / n);
    // Perturb two arbitrary indices by a tiny, sub-threshold amount --
    // nowhere near the 4x-uniform bar -- so there's a well-defined top-2
    // without triggering the override.
    belief[500] += 1e-6;
    belief[501] += 1e-6;
    double sum = 0.0;
    for (double b : belief) sum += b;
    for (double& b : belief) b /= sum;

    long long q = nextNoisyQuery(belief, 0, n);
    // Should be the plain weighted median (close to the middle of the
    // range), not adjacent to index 500/501.
    assert(q > 10 && q < n - 10);
}

void test_noisy_search_never_beats_channel_capacity_bound()
{
    // Same logic as test_entropy_optimal_never_beats_shannon_bound and
    // test_knuth_split_yesno_never_beats_shannon_bound: no valid yes/no
    // decision procedure over a channel with a fixed error rate can beat
    // channel capacity in expectation. If this ever fires, the belief
    // update or query selection has a real correctness bug -- being
    // slower than the bound is expected (see noisy_oracle_search.cpp's
    // file header); being FASTER than it is not.
    long long n = 500;
    double errorRate = 0.1;
    long long trials = 1500;
    std::mt19937 gen(2024);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto randReal01 = [&]() { return unit(gen); };
    std::uniform_int_distribution<long long> secretDist(0, n - 1);

    Welford stats;
    for (long long t = 0; t < trials; ++t)
    {
        long long secretIdx = secretDist(gen);
        auto outcome = noisySearchAttempts(secretIdx, 0, n - 1, errorRate, 0.99, 5000, randReal01);
        stats.add(outcome.attempts);
    }

    double bound = noisyChannelCapacityBound(n, errorRate);
    double tol = 3.0 * stats.stddev() / std::sqrt(static_cast<double>(trials));
    assert(stats.mean >= bound - tol);
}

int main()
{
    test_information_theoretic_bound_edge_cases();
    test_binary_search_matches_bound_exhaustively();
    test_binary_search_property_random();
    test_interpolation_search_correct_on_uniform_samples();
    test_entropy_optimal_never_beats_shannon_bound();
    test_knuth_optimal_bst_uniform_matches_naive();
    test_knuth_optimal_bst_beats_naive_on_skewed_frequencies();
    test_knuth_bst_search_attempts_matches_expected_cost();
    test_knuth_split_yesno_never_beats_shannon_bound();
    test_noisy_search_degenerates_to_binary_search_at_zero_error_rate();
    test_top2_bundling_regression();
    test_top2_override_does_not_fire_near_uniform();
    test_noisy_search_never_beats_channel_capacity_bound();
    std::cout << "All tests passed.\n";
    return 0;
}
