#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <iomanip>
#include <limits>
#include <algorithm>
#include <chrono>
#include <cstdlib>

using namespace std;

// ============================================================
// Constants
// ============================================================

const double SEQ_RMAX = 0.016;
const double SEQ_EPS = 1e-9;

const int SEQ_MAX_WEIGHT_UNITS = 4; // 20% = 4 units
const int SEQ_TOTAL_UNITS = 20;     // 100% = 20 units

// ============================================================
// Result structure
// ============================================================

struct SequentialResult
{
    vector<string> tickers;
    vector<double> bestAllocation;

    long long evaluatedPortfolios = 0;
    long long riskFeasiblePortfolios = 0;

    double bestReturn =
        -numeric_limits<double>::infinity();

    double bestRisk = 0.0;

    double executionTime = 0.0;
};

// ============================================================
// Load optimization input
//
// Reads:
// data/Optimization_Input-Table 1.csv
//
// Columns used:
// Ticker
// Expected Return (Daily)
// ============================================================

bool loadSequentialExpectedReturns(
    const string &filename,
    vector<string> &tickers,
    vector<double> &expectedReturns)
{
    ifstream file(filename);

    if (!file.is_open())
    {
        cerr << "Sequential search error: Could not open "
             << filename << endl;

        return false;
    }

    string line;

    // Skip header
    getline(file, line);

    while (getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        stringstream ss(line);

        string ticker;
        string returnValue;

        getline(ss, ticker, ',');
        getline(ss, returnValue, ',');

        if (ticker.empty() || returnValue.empty())
        {
            continue;
        }

        tickers.push_back(ticker);
        expectedReturns.push_back(stod(returnValue));
    }

    file.close();

    return true;
}

// ============================================================
// Load covariance matrix
//
// Reads:
// data/Covariance-Table 1.csv
//
// First column = ticker
// Remaining columns = covariance values
// ============================================================

bool loadSequentialCovariance(
    const string &filename,
    vector<vector<double>> &covariance)
{
    ifstream file(filename);

    if (!file.is_open())
    {
        cerr << "Sequential search error: Could not open "
             << filename << endl;

        return false;
    }

    string line;

    // Skip header
    getline(file, line);

    while (getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        stringstream ss(line);

        string value;

        // Skip ticker
        getline(ss, value, ',');

        vector<double> row;

        while (getline(ss, value, ','))
        {
            if (!value.empty())
            {
                row.push_back(stod(value));
            }
        }

        if (!row.empty())
        {
            covariance.push_back(row);
        }
    }

    file.close();

    return true;
}

// ============================================================
// Calculate portfolio expected return
//
// Return = Σ(w_i × μ_i)
//
// Used for FINAL verification only.
// ============================================================

double calculateSequentialReturn(
    const vector<double> &allocation,
    const vector<double> &expectedReturns)
{
    double result = 0.0;

    for (int i = 0; i < allocation.size(); ++i)
    {
        result +=
            allocation[i] *
            expectedReturns[i];
    }

    return result;
}

// ============================================================
// Calculate portfolio risk
//
// Risk = sqrt(w^T Σ w)
//
// Used for FINAL verification only.
// ============================================================

double calculateSequentialRisk(
    const vector<double> &allocation,
    const vector<vector<double>> &covariance)
{
    int n =
        static_cast<int>(allocation.size());

    double variance = 0.0;

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            variance +=
                allocation[i] *
                allocation[j] *
                covariance[i][j];
        }
    }

    if (variance < 0.0 &&
        variance > -SEQ_EPS)
    {
        variance = 0.0;
    }

    return sqrt(max(0.0, variance));
}

// ============================================================
// Check allocation = 100%
// ============================================================

bool isSequentialValidAllocation(
    const vector<double> &allocation)
{
    double total = 0.0;

    for (double weight : allocation)
    {
        total += weight;
    }

    return abs(total - 1.0) < SEQ_EPS;
}

// ============================================================
// Sequential exhaustive recursion
//
// 1 unit = 5%
// 20 units = 100%
//
// IMPORTANT:
// Only mathematical feasibility pruning is used.
//
// No:
// - branch and bound
// - return pruning
// - risk pruning during partial search
// - heuristic pruning
// - greedy search
// - approximation
//
// Therefore every feasible COMPLETE portfolio is evaluated.
//
// Performance improvement:
// Return and variance are accumulated incrementally.
// This does NOT change the brute-force search space.
// ============================================================

void sequentialSearch(
    int assetIndex,
    int remainingUnits,
    vector<double> &allocation,
    const vector<double> &expectedReturns,
    const vector<vector<double>> &covariance,

    // Incremental values
    double partialReturn,
    double partialVariance,

    long long &evaluatedPortfolios,
    long long &riskFeasiblePortfolios,
    double &bestReturn,
    double &bestRisk,
    vector<double> &bestAllocation)
{
    int n =
        static_cast<int>(allocation.size());

    // ========================================================
    // FEASIBILITY PRUNING
    // ========================================================

    int remainingAssets =
        n - assetIndex;

    if (remainingUnits < 0)
    {
        return;
    }

    if (remainingUnits >
        remainingAssets * SEQ_MAX_WEIGHT_UNITS)
    {
        return;
    }

    // ========================================================
    // BASE CASE
    // ========================================================

    if (assetIndex == n)
    {
        if (remainingUnits != 0)
        {
            return;
        }

        // ----------------------------------------------------
        // Complete feasible allocation
        // ----------------------------------------------------

        ++evaluatedPortfolios;

        // ----------------------------------------------------
        // Extra verification using actual percentages
        // ----------------------------------------------------

        if (!isSequentialValidAllocation(allocation))
        {
            return;
        }

        // ----------------------------------------------------
        // Risk from incremental variance
        // ----------------------------------------------------

        double currentRisk =
            sqrt(max(0.0, partialVariance));

        // ----------------------------------------------------
        // Risk constraint
        // ----------------------------------------------------

        if (currentRisk >
            SEQ_RMAX + SEQ_EPS)
        {
            return;
        }

        ++riskFeasiblePortfolios;

        // ----------------------------------------------------
        // Return from incremental calculation
        // ----------------------------------------------------

        double currentReturn =
            partialReturn;

        // ----------------------------------------------------
        // Update best portfolio
        // ----------------------------------------------------

        if (currentReturn > bestReturn)
        {
            bestReturn =
                currentReturn;

            bestRisk =
                currentRisk;

            bestAllocation =
                allocation;
        }

        return;
    }

    // ========================================================
    // DETERMINE VALID WEIGHT RANGE
    // ========================================================

    int assetsAfter =
        n - assetIndex - 1;

    int minUnits =
        max(
            0,
            remainingUnits -
                SEQ_MAX_WEIGHT_UNITS * assetsAfter);

    int maxUnits =
        min(
            SEQ_MAX_WEIGHT_UNITS,
            remainingUnits);

    // ========================================================
    // TRY EVERY ALLOWED WEIGHT
    //
    // 0%, 5%, 10%, 15%, 20%
    // ========================================================

    for (int units = minUnits;
         units <= maxUnits;
         ++units)
    {
        // ----------------------------------------------------
        // Current asset weight
        // ----------------------------------------------------

        double weight =
            units * 0.05;

        allocation[assetIndex] =
            weight;

        // ====================================================
        // INCREMENTAL RETURN
        // ====================================================

        double newReturn =
            partialReturn +
            weight *
                expectedReturns[assetIndex];

        // ====================================================
        // INCREMENTAL VARIANCE
        //
        // New variance =
        //
        // Old variance
        // + wi^2 * Cov(i,i)
        // + wi*wj*Cov(i,j)
        // + wj*wi*Cov(j,i)
        //
        // Only interactions involving the NEW asset
        // are calculated.
        // ====================================================

        double newVariance =
            partialVariance;

        // Diagonal term
        newVariance +=
            weight *
            weight *
            covariance[assetIndex][assetIndex];

        // Cross terms with previous assets
        for (int j = 0;
             j < assetIndex;
             ++j)
        {
            double previousWeight =
                allocation[j];

            newVariance +=
                weight *
                previousWeight *
                (
                    covariance[assetIndex][j] +
                    covariance[j][assetIndex]
                );
        }

        // ====================================================
        // RECURSE
        // ====================================================

        sequentialSearch(
            assetIndex + 1,
            remainingUnits - units,
            allocation,
            expectedReturns,
            covariance,
            newReturn,
            newVariance,
            evaluatedPortfolios,
            riskFeasiblePortfolios,
            bestReturn,
            bestRisk,
            bestAllocation);
    }

    // ========================================================
    // BACKTRACK
    // ========================================================

    allocation[assetIndex] = 0.0;
}

// ============================================================
// FUNCTION CALLED FROM main.cpp
//
// requestedN controls input size.
//
// Example:
// seq.exe 10
// seq.exe 12
// seq.exe 20
// ============================================================

void runSequentialExhaustive(int requestedN)
{
    cout << "\n";
    cout << "============================================"
         << endl;
    cout << "     SEQUENTIAL EXHAUSTIVE SEARCH"
         << endl;
    cout << "============================================"
         << endl;

    // ========================================================
    // FILE PATHS
    //
    // Run executable from PROJECT ROOT.
    // Example:
    // g++ src\sequential_exhaustive.cpp -O3 -o seq.exe
    // .\seq.exe 10
    // ========================================================

    const string expectedReturnFile =
        "data/Optimization_Input-Table 1.csv";

    const string covarianceFile =
        "data/Covariance-Table 1.csv";

    // ========================================================
    // LOAD DATA
    // ========================================================

    vector<string> tickers;
    vector<double> expectedReturns;
    vector<vector<double>> covariance;

    if (!loadSequentialExpectedReturns(
            expectedReturnFile,
            tickers,
            expectedReturns))
    {
        return;
    }

    if (!loadSequentialCovariance(
            covarianceFile,
            covariance))
    {
        return;
    }

    // ========================================================
    // VALIDATE REQUESTED INPUT SIZE
    // ========================================================

    int totalAssets =
        static_cast<int>(tickers.size());

    if (requestedN < 5 ||
        requestedN > totalAssets)
    {
        cout << "Error: Number of assets must be "
             << "between 5 and "
             << totalAssets
             << "."
             << endl;

        return;
    }

    // ========================================================
    // SELECT FIRST N ASSETS
    //
    // This is ONLY for testing/scaling.
    //
    // The algorithm itself remains exhaustive.
    // ========================================================

    tickers.resize(requestedN);

    expectedReturns.resize(requestedN);

    covariance.resize(requestedN);

    for (int i = 0;
         i < requestedN;
         ++i)
    {
        covariance[i].resize(requestedN);
    }

    int n =
        requestedN;

    // ========================================================
    // DISPLAY BASIC INFORMATION
    // ========================================================

    cout << "Number of assets: "
         << n
         << endl;

    cout << "Risk Limit (Rmax): "
         << fixed
         << setprecision(10)
         << SEQ_RMAX
         << endl;

    cout << "Allocation unit: 5%"
         << endl;

    cout << "Maximum per asset: 20%"
         << endl;

    cout << "Total allocation: 100%"
         << endl;

    // ========================================================
    // CALCULATE EXPECTED FEASIBLE PORTFOLIO COUNT
    //
    // Dynamic programming count.
    // This is NOT used for pruning.
    // It is only for displaying the expected count.
    // ========================================================

    vector<unsigned long long> count(
        SEQ_TOTAL_UNITS + 1,
        0);

    count[0] = 1;

    for (int asset = 0;
         asset < n;
         ++asset)
    {
        vector<unsigned long long> nextCount(
            SEQ_TOTAL_UNITS + 1,
            0);

        for (int used = 0;
             used <= SEQ_TOTAL_UNITS;
             ++used)
        {
            if (count[used] == 0)
            {
                continue;
            }

            for (int units = 0;
                 units <= SEQ_MAX_WEIGHT_UNITS;
                 ++units)
            {
                if (used + units <=
                    SEQ_TOTAL_UNITS)
                {
                    nextCount[used + units] +=
                        count[used];
                }
            }
        }

        count = nextCount;
    }

    unsigned long long expectedFeasiblePortfolios =
        count[SEQ_TOTAL_UNITS];

    cout << "Expected feasible portfolios: "
         << expectedFeasiblePortfolios
         << endl;

    // ========================================================
    // INITIALIZE SEARCH
    // ========================================================

    vector<double> allocation(
        n,
        0.0);

    vector<double> bestAllocation(
        n,
        0.0);

    long long evaluatedPortfolios = 0;

    long long riskFeasiblePortfolios = 0;

    double bestReturn =
        -numeric_limits<double>::infinity();

    double bestRisk = 0.0;

    // ========================================================
    // START TIMER
    // ========================================================

    auto startTime =
        chrono::high_resolution_clock::now();

    // ========================================================
    // RUN SEQUENTIAL EXHAUSTIVE SEARCH
    // ========================================================

    sequentialSearch(
        0,
        SEQ_TOTAL_UNITS,
        allocation,
        expectedReturns,
        covariance,

        // Initial partial return
        0.0,

        // Initial partial variance
        0.0,

        evaluatedPortfolios,
        riskFeasiblePortfolios,
        bestReturn,
        bestRisk,
        bestAllocation);

    // ========================================================
    // STOP TIMER
    // ========================================================

    auto endTime =
        chrono::high_resolution_clock::now();

    chrono::duration<double> elapsed =
        endTime - startTime;

    // ========================================================
    // FINAL VERIFICATION
    //
    // Recalculate the selected portfolio using the original
    // full equations.
    // ========================================================

    double verifiedReturn = 0.0;
    double verifiedRisk = 0.0;

    if (bestReturn !=
        -numeric_limits<double>::infinity())
    {
        verifiedReturn =
            calculateSequentialReturn(
                bestAllocation,
                expectedReturns);

        verifiedRisk =
            calculateSequentialRisk(
                bestAllocation,
                covariance);

        bestReturn =
            verifiedReturn;

        bestRisk =
            verifiedRisk;
    }

    // ========================================================
    // DISPLAY RESULTS
    // ========================================================

    cout << "\n";
    cout << "============================================"
         << endl;

    cout << "Evaluated Portfolios: "
         << evaluatedPortfolios
         << endl;

    cout << "Risk-Feasible Portfolios: "
         << riskFeasiblePortfolios
         << endl;

    // ========================================================
    // CHECK EXHAUSTIVE COUNT
    // ========================================================

    if (static_cast<unsigned long long>(
            evaluatedPortfolios)
        ==
        expectedFeasiblePortfolios)
    {
        cout << "Portfolio Count Check: PASS"
             << endl;
    }
    else
    {
        cout << "Portfolio Count Check: CHECK"
             << endl;
    }

    // ========================================================
    // DISPLAY BEST PORTFOLIO
    // ========================================================

    if (bestReturn ==
        -numeric_limits<double>::infinity())
    {
        cout << "\nNo portfolio satisfies Rmax."
             << endl;
    }
    else
    {
        cout << "\nOptimal Portfolio:"
             << endl;

        cout << "\nExpected Return (daily): "
             << fixed
             << setprecision(10)
             << bestReturn
             << endl;

        cout << "Portfolio Risk (daily):  "
             << fixed
             << setprecision(10)
             << bestRisk
             << endl;

        cout << "Rmax:                    "
             << fixed
             << setprecision(10)
             << SEQ_RMAX
             << endl;

        // ====================================================
        // PORTFOLIO ALLOCATION
        // Only non-zero allocations are shown.
        // ====================================================

        cout << "\nPortfolio Allocation:"
             << endl;

        cout << left
             << setw(10)
             << "Ticker"
             << setw(15)
             << "Allocation"
             << endl;

        cout << "-----------------------------------"
             << endl;

        double totalAllocation = 0.0;

        for (int i = 0;
             i < n;
             ++i)
        {
            if (bestAllocation[i] >
                SEQ_EPS)
            {
                cout << left
                     << setw(10)
                     << tickers[i]
                     << setw(15)
                     << fixed
                     << setprecision(0)
                     << bestAllocation[i] * 100.0
                     << "%"
                     << endl;
            }

            totalAllocation +=
                bestAllocation[i];
        }

        cout << "\nTotal Allocation: "
             << fixed
             << setprecision(0)
             << totalAllocation * 100.0
             << "%"
             << endl;
    }

    // ========================================================
    // EXECUTION TIME
    // ========================================================

    cout << "\nSequential Execution Time: "
         << fixed
         << setprecision(6)
         << elapsed.count()
         << " seconds"
         << endl;

    cout << "============================================"
         << endl;
}

// ============================================================
// TEMPORARY STANDALONE MAIN
//
// Usage:
//   .\seq.exe
//       -> defaults to 10 assets
//
//   .\seq.exe 10
//       -> 10 assets
//
//   .\seq.exe 12
//       -> 12 assets
//
//   .\seq.exe 20
//       -> full 20-asset problem
//
// Remove this main when integrating with team's main.cpp.
// ============================================================

int main(int argc, char *argv[])
{
    int requestedN = 10;

    if (argc >= 2)
    {
        requestedN =
            atoi(argv[1]);
    }

    runSequentialExhaustive(
        requestedN);

    return 0;
}
