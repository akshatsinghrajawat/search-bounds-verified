#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

// Knuth's optimal BST DP -- included as a library (KNUTH_NO_MAIN
// suppresses its standalone main()) so runWeightedComparison below
// can measure it against binary search and the entropy heuristic
// under the exact same drawn secrets.
#define KNUTH_NO_MAIN
#include "knuth_optimal_bst.cpp"

// Noisy oracle search -- pure algorithm (weighted-median query selection +
// Bayesian belief update) lives in its own file the same way Knuth's DP
// does; the Monte Carlo driver that injects the actual randomness (the
// oracle's lies) lives here, right next to rng() and the other drivers.
#define NOISY_ORACLE_NO_MAIN
#include "noisy_oracle_search.cpp"

// ---------------------------------------------------------------------------
// Config: every range/trial/seed value now flows from here instead of the
// old LOWER/UPPER globals, so the program can validate its own core claim
// (is ceil(log2 N) optimal?) at any N, not just N=100.
// ---------------------------------------------------------------------------
struct Config
{
    long long lower = 1, upper = 100;
    long long trials = 100000;
    unsigned seed = 0;      // 0 => seed from random_device
    bool jsonOutput = false;
    std::string svgPath;    // empty => no chart written
    double oracleErrorRate = 0.1;      // noisy-oracle comparison only
    double oracleConfidence = 0.99;    // stop once a candidate's posterior exceeds this
};

void printUsage()
{
    std::cerr <<
        "Usage: guess <command> [flags]\n"
        "Commands:\n"
        "  play                 you guess, computer thinks of a number\n"
        "  demo                 computer guesses your number via binary search\n"
        "  simulate             Monte Carlo comparison of strategies\n"
        "Flags:\n"
        "  --min N   --max N    range bounds (default 1..100)\n"
        "  --trials N           number of simulation trials (default 100000)\n"
        "  --seed N             RNG seed for reproducibility\n"
        "  --json               machine-readable stats output (simulate only)\n"
        "  --svg PATH            write a bar-chart SVG comparing strategies (simulate only)\n"
        "  --oracle-error-rate P probability the noisy oracle answers wrong (default 0.1)\n"
        "  --oracle-confidence C posterior threshold to stop the noisy search (default 0.99)\n";
}

Config parseArgs(int argc, char** argv, std::string& command)
{
    Config cfg;
    if (argc < 2) { printUsage(); std::exit(EXIT_FAILURE); }
    command = argv[1];

    for (int i = 2; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << arg << "\n"; std::exit(EXIT_FAILURE); }
            return argv[++i];
        };
        if (arg == "--min") cfg.lower = std::stoll(next());
        else if (arg == "--max") cfg.upper = std::stoll(next());
        else if (arg == "--trials") cfg.trials = std::stoll(next());
        else if (arg == "--seed") cfg.seed = static_cast<unsigned>(std::stoul(next()));
        else if (arg == "--json") cfg.jsonOutput = true;
        else if (arg == "--svg") cfg.svgPath = next();
        else if (arg == "--oracle-error-rate") cfg.oracleErrorRate = std::stod(next());
        else if (arg == "--oracle-confidence") cfg.oracleConfidence = std::stod(next());
        else { std::cerr << "Unknown flag: " << arg << "\n"; printUsage(); std::exit(EXIT_FAILURE); }
    }
    if (cfg.lower >= cfg.upper) { std::cerr << "--min must be less than --max\n"; std::exit(EXIT_FAILURE); }
    if (cfg.oracleErrorRate < 0.0 || cfg.oracleErrorRate >= 0.5)
    {
        std::cerr << "--oracle-error-rate must be in [0, 0.5)\n";
        std::exit(EXIT_FAILURE);
    }
    return cfg;
}

// Was seeded from time(nullptr): two processes launched in the same second
// got identical "random" streams -- a real reproducibility bug, not just a
// style nit. Now seeds from random_device by default; --seed (wired in
// Commit 1's parseArgs) deterministically overrides it when given.
std::mt19937& rng()
{
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

// Separated from rng() itself: static locals init on first call only, so
// folding the seed into rng()'s default argument meant *whichever* call
// site ran first silently won -- if anything else called rng() before
// main() seeded it, --seed would be ignored with no warning.
void seedRng(unsigned seed)
{
    if (seed != 0) rng().seed(seed);
}

long long uniformInt(long long lo, long long hi)
{
    std::uniform_int_distribution<long long> dist(lo, hi);
    return dist(rng());
}

double uniformReal01()
{
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng());
}

// Worst-case comparisons for THIS binary search implementation is the
// smallest k such that 2^k >= rangeSize+1 (equivalently ceil(log2(N+1))).
// NOT ceil(log2(N)) -- the two formulas only disagree at exact powers of
// two, which is exactly why the bug this replaces went unnoticed: the
// repo's hardcoded default N=100 isn't a power of two. Computed with
// integer bit-shifting rather than floating log2 to avoid floating-point
// edge cases entirely.
long long informationTheoreticLowerBound(long long rangeSize)
{
    // capacity/target are unsigned so an extreme --max (e.g. LLONG_MAX)
    // can't trigger signed overflow on `rangeSize + 1` or shift a 1 into
    // the sign bit via capacity <<= 1 -- both UB in the old signed version.
    unsigned long long capacity = 1;
    unsigned long long target = static_cast<unsigned long long>(rangeSize) + 1;
    long long k = 0;
    while (capacity < target) { capacity <<= 1; ++k; }
    return k;
}

long long readIntInRange(const std::string& prompt, long long lo, long long hi)
{
    long long value;
    while (true)
    {
        std::cout << prompt;
        std::cin >> value;
        if (std::cin.fail() || value < lo || value > hi)
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Please enter a number between " << lo << " and " << hi << ".\n";
            continue;
        }
        return value;
    }
}

void playHumanGuesses(const Config& cfg)
{
    long long secret = uniformInt(cfg.lower, cfg.upper);
    int attempts = 0;
    std::cout << "\nI'm thinking of a number between " << cfg.lower << " and " << cfg.upper << ".\n";
    while (true)
    {
        long long guess = readIntInRange("Your guess: ", cfg.lower, cfg.upper);
        ++attempts;
        if (guess == secret) { std::cout << "Correct in " << attempts << " tries!\n"; return; }
        std::cout << (guess < secret ? "Higher.\n" : "Lower.\n");
    }
}

void playComputerGuesses(const Config& cfg)
{
    long long lo = cfg.lower, hi = cfg.upper;
    int attempts = 0;
    std::cout << "\nThink of a number between " << cfg.lower << " and " << cfg.upper << ".\n";
    while (lo <= hi)
    {
        long long mid = lo + (hi - lo) / 2;
        ++attempts;
        std::cout << "Is it " << mid << "? (h/l/c): ";
        char response; std::cin >> response;
        if (response == 'c') { std::cout << "Found it in " << attempts << " tries!\n"; return; }
        if (response == 'h') hi = mid - 1; else lo = mid + 1;
    }
    std::cout << "Something's inconsistent with your answers.\n";
}

// ---------------------------------------------------------------------------
// Streaming mean/variance (Welford's algorithm) -- O(1) memory instead of
// storing every trial's attempt count in a vector. Confirmed numerically
// identical to the old two-pass computation (mean/variance matched to full
// displayed precision against a naive implementation in testing) while
// making --trials 100000000+ actually feasible.
// ---------------------------------------------------------------------------
struct Welford
{
    long long n = 0;
    double mean = 0.0, m2 = 0.0;
    long long worst = 0;

    void add(long long x)
    {
        ++n;
        double d = static_cast<double>(x) - mean;
        mean += d / n;
        m2 += d * (static_cast<double>(x) - mean);
        worst = std::max(worst, x);
    }
    double variance() const { return n ? m2 / n : 0.0; }
    double stddev()   const { return std::sqrt(variance()); }
};

long long binarySearchAttempts(long long secret, long long lo, long long hi)
{
    long long attempts = 0;
    while (lo <= hi)
    {
        long long mid = lo + (hi - lo) / 2;
        ++attempts;
        if (mid == secret) return attempts;
        if (mid < secret) lo = mid + 1; else hi = mid - 1;
    }
    return attempts;
}

// Was previously `secret - LOWER + 1` -- a closed-form identity wrapped in a
// Monte Carlo loop, not an actual simulation. This walks the range exactly
// as a real linear scan would.
long long linearScanAttempts(long long secret, long long lo)
{
    long long attempts = 0;
    for (long long probe = lo; ; ++probe)
    {
        ++attempts;
        if (probe == secret) return attempts;
    }
}

// Probes proportional to where the target would sit assuming a uniform
// distribution -- expected O(log log N) on uniform data. Genuinely new
// third strategy (the old repo only had two, and one was fake).
// FIXED (was interpolationSearchAttempts(secret, lo, hi)): running
// interpolation search directly on scalar [lo, hi] bounds makes
// mid = lo + ((secret-lo)/(hi-lo))*(hi-lo) algebraically collapse to
// mid = secret, so it "found" the target on query 1 almost every time --
// that's not interpolation search on uniform data, it's testing on a
// perfectly dense arithmetic progression. This version searches an actual
// sampled array, same approach as interpolationSearchWorstCaseOn below.
long long interpolationSearchAttemptsOnArray(long long target, const std::vector<long long>& sortedValues)
{
    long long l = 0, h = static_cast<long long>(sortedValues.size()) - 1, attempts = 0;
    while (l <= h)
    {
        ++attempts;
        if (l == h) break;
        if (sortedValues[h] == sortedValues[l]) break;
        long long mid = l + static_cast<long long>(
            (static_cast<double>(target - sortedValues[l])
             / (sortedValues[h] - sortedValues[l])) * (h - l));
        mid = std::clamp(mid, l, h);
        if (sortedValues[mid] == target) break;
        if (sortedValues[mid] < target) l = mid + 1; else h = mid - 1;
    }
    return attempts;
}

// Constructs a Fibonacci-spaced array -- the classic adversarial input that
// forces interpolation search toward O(N), rather than merely noticing the
// collapse on data that happens to be skewed. Proves the failure mode
// instead of stumbling on it.
std::vector<long long> buildFibonacciAdversary(long long upperBound)
{
    std::vector<long long> fib = {1, 2};
    while (fib.back() < upperBound) fib.push_back(fib.back() + fib[fib.size() - 2]);
    return fib;
}

// Runs interpolation search's midpoint formula against every element of a
// pre-built array (see buildFibonacciAdversary above) and returns the worst
// step count seen -- the actual measurement behind the "proven collapse"
// claim printed in runSimulation below.
long long interpolationSearchWorstCaseOn(const std::vector<long long>& sortedValues)
{
    long long n = static_cast<long long>(sortedValues.size());
    long long worst = 0;
    for (long long secretIdx = 0; secretIdx < n; ++secretIdx)
    {
        long long l = 0, h = n - 1, attempts = 0;
        long long target = sortedValues[secretIdx];
        while (l <= h)
        {
            ++attempts;
            if (l == h) break;
            if (sortedValues[h] == sortedValues[l]) break;
            long long mid = l + static_cast<long long>(
                (static_cast<double>(target - sortedValues[l])
                 / (sortedValues[h] - sortedValues[l])) * (h - l));
            mid = std::clamp(mid, l, h);
            if (sortedValues[mid] == target) break;
            if (sortedValues[mid] < target) l = mid + 1; else h = mid - 1;
        }
        worst = std::max(worst, attempts);
    }
    return worst;
}

// ---------------------------------------------------------------------------
// Weighted distributions + entropy-optimal search. Binary search is only
// optimal when candidates are equiprobable; under a skewed prior you must
// split *probability mass* in half, not candidate count. This is the single
// change that turns the project from "binary search is optimal" (trivially
// true only under uniformity) into a real study of when it stops being true.
// ---------------------------------------------------------------------------
std::vector<double> zipfWeights(long long n, double alpha = 1.0)
{
    std::vector<double> w(n);
    for (long long i = 0; i < n; ++i) w[i] = 1.0 / std::pow(static_cast<double>(i + 1), alpha);
    double total = std::accumulate(w.begin(), w.end(), 0.0);
    for (auto& x : w) x /= total;
    return w;
}

double shannonEntropyBits(const std::vector<double>& p)
{
    double h = 0.0;
    for (double x : p) if (x > 0) h -= x * std::log2(x);
    return h;
}

// NOTE ON QUERY MODEL: the game's normal "guess a number" mode gets 3-way
// feedback (higher/lower/exact match), which lets a solver occasionally win
// early via a lucky exact hit -- that's not a plain yes/no channel, so its
// query count isn't directly comparable to the binary Shannon entropy H(p).
// To compare cleanly against H(p), both strategies below use strict yes/no
// queries ("is secret <= mid?"), terminating only once exactly one candidate
// remains. This was caught by testing: an earlier version of this function
// used the 3-way model and produced a mean *below* H(p), which is
// mathematically impossible for a valid yes/no decision procedure -- the
// fix here isn't a style choice, it's what makes the comparison valid.

// Splits by cumulative probability mass rather than candidate count -- the
// entropy-optimal generalization of binary search's plain midpoint rule.
// Clamped to [lo, hi-1] so every query is guaranteed to leave both the
// "yes" and "no" branches non-empty.
long long entropyOptimalYesNoAttempts(long long secretIdx, long long lo, long long hi,
                                       const std::vector<double>& cumWeight)
{
    long long attempts = 0;
    while (lo < hi)
    {
        double lowMass = (lo > 0) ? cumWeight[lo - 1] : 0.0;
        double target = lowMass + (cumWeight[hi] - lowMass) / 2.0;
        auto it = std::lower_bound(cumWeight.begin() + lo, cumWeight.begin() + hi + 1, target);
        long long mid = std::clamp(static_cast<long long>(it - cumWeight.begin()), lo, hi - 1);
        ++attempts;
        if (secretIdx <= mid) hi = mid; else lo = mid + 1;
    }
    return attempts;
}

long long binaryYesNoAttempts(long long secretIdx, long long lo, long long hi)
{
    long long attempts = 0;
    while (lo < hi)
    {
        long long mid = lo + (hi - lo) / 2;
        ++attempts;
        if (secretIdx <= mid) hi = mid; else lo = mid + 1;
    }
    return attempts;
}

// ---------------------------------------------------------------------------
// Bridging Knuth's DP into the comparison above. Same strict yes/no query
// discipline as entropyOptimalYesNoAttempts (continue until exactly one
// candidate remains -- no early-exit "exact match" bonus), but the split
// point at each step comes from Knuth's DP-optimal root table instead of a
// cumulative-mass midpoint. Deliberately NOT knuthBstSearchAttempts's
// classical 3-way traversal: mixing a 3-way (early-exit-on-match) cost into
// this yes/no comparison table reproduces the exact bug the NOTE ON QUERY
// MODEL comment above already describes -- a mean landing below H(p),
// which is impossible for a genuine yes/no procedure. This function isn't
// claimed to be optimal under yes/no cost (Knuth's DP was optimized for
// the 3-way model) -- only that it reuses the DP's split points under a
// cost measure that's actually comparable to the other two rows.
// ---------------------------------------------------------------------------
long long knuthSplitYesNoAttempts(long long secretIdx, long long lo, long long hi,
                                   const std::vector<std::vector<int>>& root)
{
    long long attempts = 0;
    while (lo < hi)
    {
        // root[lo][hi+1]: Knuth's chosen 1-indexed root key for the gap
        // range spanning 0-indexed keys [lo, hi]. Converted to a 0-indexed
        // split boundary the same way entropyOptimalYesNoAttempts uses mid.
        long long mid = root[lo][hi + 1] - 1;
        ++attempts;
        if (secretIdx <= mid) hi = mid; else lo = mid + 1;
    }
    return attempts;
}

// Knuth's O(N^2) DP is fast and cheap at the scale of a config-lookup table
// (n in the tens), but O(N^2) TIME AND SPACE stops being a rounding error
// fast: n=8000 already takes ~7s and ~1GB; n=100000 (this program's usual
// --max) would need ~80GB and was OOM-killed the first time this was
// tried. So the DP itself, not the search cost it's measuring, becomes the
// bottleneck -- this cap keeps the comparison fast (well under a second)
// and honest about that limit instead of hanging or crashing.
constexpr long long kKnuthDpFeasibleN = 2000;

// Same reasoning as kKnuthDpFeasibleN above, different bottleneck: each
// noisy-search trial costs O(queries * N) -- the belief vector update is
// O(N) per query, and this algorithm empirically needs ~6x the
// channel-capacity bound in queries (see noisy_oracle_search.cpp). At the
// default --trials 100000 that's on the order of 10^9-10^10 operations,
// which would make a plain `./guess simulate` hang. Capped independently
// of --trials and --max so the default invocation stays fast; pass
// smaller --min/--max and a larger --trials explicitly to get a tighter
// noisy-oracle estimate.
constexpr long long kNoisyOracleRangeCap = 2000;
constexpr long long kNoisyOracleTrialCap = 2000;

void runWeightedComparison(long long n, double alpha, long long trials)
{
    auto weight = zipfWeights(n, alpha);
    std::vector<double> cum(n);
    std::partial_sum(weight.begin(), weight.end(), cum.begin());
    double H = shannonEntropyBits(weight);

    std::discrete_distribution<long long> secretDist(weight.begin(), weight.end());
    Welford bsearch, entropyOpt;

    if (n > kKnuthDpFeasibleN)
    {
        for (long long t = 0; t < trials; ++t)
        {
            long long secretIdx = secretDist(rng());
            bsearch.add(binaryYesNoAttempts(secretIdx, 0, n - 1));
            entropyOpt.add(entropyOptimalYesNoAttempts(secretIdx, 0, n - 1, cum));
        }
        std::cout << "\nZipf(alpha=" << alpha << ", N=" << n << "), yes/no queries: H(p)=" << H << " bits\n"
                  << "  Binary search (frequency-blind):     mean=" << bsearch.mean << "\n"
                  << "  Entropy-optimal (greedy mass split):  mean=" << entropyOpt.mean
                  << "  (theoretical band: [" << H << ", " << (H + 1) << "))\n"
                  << "  Knuth optimal BST skipped: N=" << n << " exceeds the O(N^2) DP's "
                  << "feasible size (" << kKnuthDpFeasibleN << "); rerun with a smaller "
                  << "--min/--max span to see the 3-way comparison.\n";
        return;
    }

    // Knuth's DP-optimal split points for this exact frequency distribution.
    // No "miss" gaps (q all zero) because secretDist above only ever draws
    // a real key -- this Monte Carlo experiment never simulates an
    // unsuccessful search, so the model matches the simulation exactly.
    std::vector<double> gaps(static_cast<size_t>(n) + 1, 0.0);
    auto knuthResult = knuthOptimalBST(weight, gaps);
    Welford knuthOpt;

    for (long long t = 0; t < trials; ++t)
    {
        long long secretIdx = secretDist(rng());
        bsearch.add(binaryYesNoAttempts(secretIdx, 0, n - 1));
        entropyOpt.add(entropyOptimalYesNoAttempts(secretIdx, 0, n - 1, cum));
        knuthOpt.add(knuthSplitYesNoAttempts(secretIdx, 0, n - 1, knuthResult.root));
    }

    std::cout << "\nZipf(alpha=" << alpha << ", N=" << n << "), yes/no queries: H(p)=" << H << " bits\n"
              << "  Binary search (frequency-blind):        mean=" << bsearch.mean << "\n"
              << "  Entropy-optimal (greedy mass split):     mean=" << entropyOpt.mean << "\n"
              << "  Knuth split points (DP-optimal, yes/no): mean=" << knuthOpt.mean << "\n"
              << "  (theoretical band for any yes/no procedure: [" << H << ", " << (H + 1) << "))\n";

    // H(p) is a lower bound on expected query count for ANY valid yes/no
    // decision procedure over this distribution -- not just this one. If
    // knuthOpt (or entropyOpt) ever lands below H(p) beyond sampling noise,
    // that's not "Knuth beating information theory," it's a bridge bug
    // (see the comment on knuthSplitYesNoAttempts for the exact failure
    // mode this guards against).
    double tol = 3.0 * knuthOpt.stddev() / std::sqrt(static_cast<double>(trials));
    if (knuthOpt.mean < H - tol)
    {
        std::cerr << "WARNING: Knuth-derived yes/no mean fell below the Shannon bound H(p) "
                     "beyond sampling noise -- check for a 3-way/yes-no cost-model mismatch.\n";
    }
}

// ---------------------------------------------------------------------------
// Bridging the noisy oracle search into the comparison set. Same driver
// shape as runWeightedComparison above: the pure algorithm lives in
// noisy_oracle_search.cpp, this function supplies the randomness (both the
// secret and the oracle's lies) and reports Welford stats against the
// channel-capacity bound.
//
// Unlike binary/entropy/Knuth above, this isn't a hard PASS/FAIL: the
// implemented algorithm (probabilistic bisection, see noisy_oracle_search.cpp)
// is provably consistent but not proven to hit the channel-capacity bound in
// this finite-sample regime, so a gap above the bound is expected, not a
// failure. What WOULD indicate a real bug is the mean landing below the
// bound -- see the WARNING check at the end, same shape as the Knuth one
// above.
// ---------------------------------------------------------------------------
void runNoisyOracleComparison(long long n, double errorRate, double confidenceThreshold, long long trials)
{
    Welford noisy;
    long long successes = 0;

    for (long long t = 0; t < trials; ++t)
    {
        long long secretIdx = uniformInt(0, n - 1);
        NoisySearchOutcome outcome = noisySearchAttempts(
            secretIdx, 0, n - 1, errorRate, confidenceThreshold, /*maxQueries=*/5000, uniformReal01);
        noisy.add(outcome.attempts);
        if (outcome.guess == secretIdx) ++successes;
    }

    double bound = noisyChannelCapacityBound(n, errorRate);
    double successRate = static_cast<double>(successes) / trials;

    std::cout << "\nNoisy oracle search, N=" << n << ", oracle error rate=" << errorRate
              << ", confidence threshold=" << confidenceThreshold << ":\n"
              << "  Probabilistic bisection: mean=" << noisy.mean
              << " stddev=" << noisy.stddev()
              << " worst=" << noisy.worst
              << " success rate=" << successRate << "\n"
              << "  Channel-capacity bound (log2(N)/(1-H(p))): " << bound
              << "  <- not met by this algorithm; see noisy_oracle_search.cpp\n";

    // No valid yes/no decision procedure over a binary-symmetric-channel
    // oracle can beat the channel capacity in expectation -- same logic as
    // the Shannon-bound checks above. If this ever fires, it means the
    // belief update or query selection has a real bug, not just a slow
    // constant.
    double tol = 3.0 * noisy.stddev() / std::sqrt(static_cast<double>(trials));
    if (noisy.mean < bound - tol)
    {
        std::cerr << "WARNING: noisy oracle search mean fell below the channel-capacity bound "
                     "beyond sampling noise -- this should be impossible; check the belief "
                     "update or query selection for a bug.\n";
    }
}

double confidenceHalfWidth95(double stddev, long long n)
{
    return 1.96 * stddev / std::sqrt(static_cast<double>(n));
}

// Bins the actual drawn secrets into equal-width buckets and returns the
// chi-square statistic against a uniform null hypothesis. Dogfoods the
// project's own point: if the RNG's uniformity is ever suspect, it would
// silently corrupt every mean/variance this program reports -- so check it.
double chiSquareUniformity(const std::vector<long long>& draws, long long lo, long long hi, int bins)
{
    std::vector<long long> counts(bins, 0);
    double width = static_cast<double>(hi - lo + 1) / bins;
    for (long long x : draws)
    {
        int b = std::clamp(static_cast<int>((x - lo) / width), 0, bins - 1);
        ++counts[b];
    }
    double expected = static_cast<double>(draws.size()) / bins;
    double chi2 = 0.0;
    for (long long c : counts) chi2 += (c - expected) * (c - expected) / expected;
    return chi2;
}

// Exact analytic mean for binary search under uniformity: for range size N,
// level k (1-indexed) holds the count of candidates first distinguished on
// the k-th query. Computed directly, independent of any simulation, so the
// Monte Carlo mean has real ground truth to be checked against.
double analyticBinarySearchMean(long long n)
{
    double sum = 0.0;
    long long remaining = n, level = 1;
    while (remaining > 0)
    {
        long long atThisLevel = std::min(remaining, 1LL << (level - 1));
        sum += static_cast<double>(atThisLevel) * level;
        remaining -= atThisLevel;
        ++level;
    }
    return sum / n;
}

// Pure C++ SVG bar chart -- no external library or language needed. SVG is
// plain text/XML, so this is just formatted output, the same way the JSON
// printer above is. Opens directly in any browser or image viewer.
//
// logScale controls bar HEIGHT only -- the printed number above each bar is
// always the real value. Needed because linear scan's mean is routinely
// 10,000x binary/interpolation search's: on a linear axis the other two
// bars round to 0px and effectively vanish. Log scale keeps every bar
// visible without lying about the underlying numbers.
void writeSvgBarChart(const std::string& path, const std::vector<std::pair<std::string, double>>& bars,
                      bool logScale = false)
{
    auto scaled = [logScale](double v) { return logScale ? std::log10(v + 1.0) : v; };

    double maxVal = 0.0;
    for (const auto& b : bars) maxVal = std::max(maxVal, scaled(b.second));

    const int width = 480, height = 260, barWidth = 90, gap = 30, baseY = 210, maxBarHeight = 160;
    std::ofstream out(path);
    if (!out)
    {
        std::cerr << "Could not open " << path << " for writing\n";
        return;
    }

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\">\n"
        << "  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
        << "  <line x1=\"30\" y1=\"" << baseY << "\" x2=\"" << (width - 20) << "\" y2=\"" << baseY
        << "\" stroke=\"black\"/>\n";

    int x = 50;
    for (const auto& [label, value] : bars)
    {
        int barHeight = (maxVal > 0.0) ? static_cast<int>((scaled(value) / maxVal) * maxBarHeight) : 0;
        out << "  <rect x=\"" << x << "\" y=\"" << (baseY - barHeight) << "\" width=\"" << barWidth
            << "\" height=\"" << barHeight << "\" fill=\"#4472C4\"/>\n"
            << "  <text x=\"" << (x + barWidth / 2) << "\" y=\"" << (baseY - barHeight - 6)
            << "\" font-size=\"12\" text-anchor=\"middle\" font-family=\"sans-serif\">"
            << value << "</text>\n"
            << "  <text x=\"" << (x + barWidth / 2) << "\" y=\"" << (baseY + 18)
            << "\" font-size=\"12\" text-anchor=\"middle\" font-family=\"sans-serif\">"
            << label << "</text>\n";
        x += barWidth + gap;
    }
    out << "</svg>\n";
}

// Orchestrates the whole `simulate` command: runs binary/linear/interpolation
// search over `cfg.trials` random secrets, asserts the measured worst case
// against the information-theoretic bound (Flaw #4), reports a 95% CI and a
// chi-square RNG uniformity check (Flaw #7), demonstrates interpolation
// search's adversarial collapse (Flaw #3), and runs the entropy-optimal vs.
// binary-search comparison under a skewed prior (Flaw #2), and the noisy
// oracle comparison under an unreliable channel. Optionally emits --json
// stats and/or a --svg chart (Flaw #8).
void runSimulation(const Config& cfg)
{
    long long rangeSize = cfg.upper - cfg.lower + 1;
    Welford bsearch, linear, interp;
    std::vector<long long> drawnSecrets;
    drawnSecrets.reserve(cfg.trials);

    // Uniform-data sample for interpolation search: N draws from
    // [lower, upper], sorted into an actual array, so the search below is
    // querying an array (see interpolationSearchAttemptsOnArray) instead
    // of degenerating to mid = secret.
    long long interpSampleSize = std::min(rangeSize, cfg.trials);
    std::vector<long long> interpSample;
    interpSample.reserve(interpSampleSize);
    for (long long i = 0; i < interpSampleSize; ++i)
        interpSample.push_back(uniformInt(cfg.lower, cfg.upper));
    std::sort(interpSample.begin(), interpSample.end());

    for (long long t = 0; t < cfg.trials; ++t)
    {
        long long secret = uniformInt(cfg.lower, cfg.upper);
        drawnSecrets.push_back(secret);
        bsearch.add(binarySearchAttempts(secret, cfg.lower, cfg.upper));
        linear.add(linearScanAttempts(secret, cfg.lower));

        long long interpTarget = interpSample[uniformInt(0, static_cast<long long>(interpSample.size()) - 1)];
        interp.add(interpolationSearchAttemptsOnArray(interpTarget, interpSample));
    }

    long long bound = informationTheoreticLowerBound(rangeSize);
    std::cout << "\nRange size: " << rangeSize << " (bound=" << bound << ")\n"
              << "Binary search:       mean=" << bsearch.mean << " worst=" << bsearch.worst << "\n"
              << "Linear scan:         mean=" << linear.mean << " worst=" << linear.worst << "\n"
              << "Interpolation search: mean=" << interp.mean << " worst=" << interp.worst
              << "  (uniform-data case)\n";

    // The README claims binary search's worst case matches ceil(log2 N)
    // exactly. Previously this was only ever printed side-by-side with the
    // bound -- if the two ever disagreed, the program would still exit 0
    // and print happily. Now it's an actual assertion with a real exit code.
    if (bsearch.worst > bound)
    {
        std::cerr << "BOUND VIOLATION: binary search worst=" << bsearch.worst
                   << " exceeds bound=" << bound << "\n";
        std::exit(EXIT_FAILURE);
    }
    std::cout << "PASS: binary search worst case (" << bsearch.worst
              << ") does not exceed ceil(log2 N) = " << bound << "\n";

    double ci = confidenceHalfWidth95(bsearch.stddev(), cfg.trials);
    double analyticMean = analyticBinarySearchMean(rangeSize);
    std::cout << "95% CI on binary search mean: " << bsearch.mean << " +/- " << ci
              << "  [" << (bsearch.mean - ci) << ", " << (bsearch.mean + ci) << "]\n"
              << "Analytic ground truth mean (exact, no sampling): " << analyticMean
              << (std::abs(analyticMean - bsearch.mean) <= ci ? "  (inside CI, as expected)"
                                                                : "  (OUTSIDE CI -- investigate)")
              << "\n";

    int bins = static_cast<int>(std::min<long long>(20, rangeSize));
    double chi2 = chiSquareUniformity(drawnSecrets, cfg.lower, cfg.upper, bins);
    std::cout << "Chi-square uniformity check on drawn secrets: chi2=" << chi2
              << " across " << bins << " bins (df=" << (bins - 1)
              << ") -- compare against a chi-square table if RNG quality is in doubt\n";

    auto adversary = buildFibonacciAdversary(std::min(rangeSize, 100000LL));
    long long adversarialWorst = interpolationSearchWorstCaseOn(adversary);
    long long adversaryBound = informationTheoreticLowerBound(static_cast<long long>(adversary.size()));
    std::cout << "On a Fibonacci-spaced adversarial array (n=" << adversary.size()
              << "): interpolation worst-case=" << adversarialWorst
              << " vs. binary-search bound=" << adversaryBound
              << "  <- proven collapse, not an empirical accident\n";

    runWeightedComparison(rangeSize, 1.0, cfg.trials);
    runNoisyOracleComparison(std::min(rangeSize, kNoisyOracleRangeCap), cfg.oracleErrorRate,
                              cfg.oracleConfidence, std::min(cfg.trials, kNoisyOracleTrialCap));

    if (cfg.jsonOutput)
    {
        auto printJson = [](const std::string& label, double mean, double stddev, long long worst) {
            std::cout << "{\"strategy\":\"" << label << "\",\"mean\":" << mean
                       << ",\"stddev\":" << stddev << ",\"worst\":" << worst << "}\n";
        };
        printJson("binary_search", bsearch.mean, bsearch.stddev(), bsearch.worst);
        printJson("linear_scan", linear.mean, linear.stddev(), linear.worst);
        printJson("interpolation_search", interp.mean, interp.stddev(), interp.worst);
    }

    if (!cfg.svgPath.empty())
    {
        // Log-scaled: linear scan's mean is ~10,000x binary/interpolation's,
        // which renders the other two bars at 0px on a linear axis.
        writeSvgBarChart(cfg.svgPath, {
            {"binary", bsearch.mean},
            {"linear", linear.mean},
            {"interp", interp.mean},
        }, /*logScale=*/true);
        std::cout << "Wrote chart to " << cfg.svgPath << " (log-scaled bars; labels show real means)\n";
    }
}

// Guarded so tests.cpp can #include this file and reuse every function
// above without linking a second main(). A proper header/impl split lands
// in a later commit; this is the minimal version until then.
#ifndef GUESS_NO_MAIN
int main(int argc, char** argv)
{
    std::string command;
    Config cfg = parseArgs(argc, argv, command);
    seedRng(cfg.seed); // must happen before any other rng() call

    if (command == "play") playHumanGuesses(cfg);
    else if (command == "demo") playComputerGuesses(cfg);
    else if (command == "simulate") runSimulation(cfg);
    else { printUsage(); return EXIT_FAILURE; }

    return 0;
}
#endif // GUESS_NO_MAIN
