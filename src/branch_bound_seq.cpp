#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

using namespace std;

// ============================================================
// PROJECT CONSTANTS
// ============================================================

constexpr int TOTAL_UNITS = 20;            // 100% / 5% = 20 units
constexpr int MAX_UNITS_PER_ASSET = 4;     // Maximum 20% per asset
constexpr double UNIT_WEIGHT = 1.0 / 20.0; // 5%
constexpr double RMAX = 0.016;             // Maximum portfolio risk

constexpr double EPSILON = 1e-12;

// ============================================================
// DATA STRUCTURES
// ============================================================

struct Asset
{
    string ticker;
    double expectedReturn;
    int originalIndex;
};

struct Solution
{
    double returnValue = -numeric_limits<double>::infinity();
    double risk = numeric_limits<double>::infinity();

    // Allocation in the B&B search order
    vector<int> weights;
};

struct SearchStats
{
    unsigned long long nodesVisited = 0;

    unsigned long long feasibilityPruned = 0;
    unsigned long long boundPruned = 0;

    unsigned long long completePortfoliosEvaluated = 0;

    // Number of complete feasible portfolios skipped because
    // of the Branch & Bound upper bound.
    unsigned long long candidatePortfoliosPruned = 0;
};

// ============================================================
// STRING / CSV UTILITIES
// ============================================================

string trim(const string &input)
{
    size_t start = input.find_first_not_of(" \t\r\n");

    if (start == string::npos)
        return "";

    size_t end = input.find_last_not_of(" \t\r\n");

    return input.substr(start, end - start + 1);
}

// Simple CSV parser.
// Handles normal CSV fields and quoted fields.
vector<string> parseCSVLine(const string &line)
{
    vector<string> fields;

    string current;
    bool insideQuotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];

        if (c == '"')
        {
            insideQuotes = !insideQuotes;
        }
        else if (c == ',' && !insideQuotes)
        {
            fields.push_back(trim(current));
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    fields.push_back(trim(current));

    return fields;
}

// Try the normal project-root path first.
// Also supports running the executable from src/.
ifstream openDataFile(const string &filename)
{
    vector<string> possiblePaths =
        {
            "data/" + filename,
            "../data/" + filename,
            filename};

    for (const string &path : possiblePaths)
    {
        ifstream file(path);

        if (file.is_open())
        {
            return file;
        }
    }

    throw runtime_error(
        "Could not open data file: " + filename +
        "\nExpected location: data/" + filename);
}

// ============================================================
// LOAD EXPECTED RETURNS
// ============================================================

vector<Asset> loadOptimizationInput()
{
    const string filename = "Optimization_Input-Table 1.csv";

    ifstream file = openDataFile(filename);

    vector<Asset> assets;

    string line;

    // Skip header
    if (!getline(file, line))
    {
        throw runtime_error("Optimization input file is empty.");
    }

    int originalIndex = 0;

    while (getline(file, line))
    {
        if (trim(line).empty())
            continue;

        vector<string> fields = parseCSVLine(line);

        if (fields.size() < 2)
            continue;

        string ticker = trim(fields[0]);

        if (ticker.empty() || ticker == "Ticker")
            continue;

        try
        {
            double expectedReturn = stod(fields[1]);

            Asset asset;

            asset.ticker = ticker;
            asset.expectedReturn = expectedReturn;
            asset.originalIndex = originalIndex++;

            assets.push_back(asset);
        }
        catch (...)
        {
            throw runtime_error(
                "Invalid expected return for ticker: " + ticker);
        }
    }

    file.close();

    if (assets.size() != 20)
    {
        throw runtime_error(
            "Expected 20 assets in Optimization_Input-Table 1.csv, "
            "but found " +
            to_string(assets.size()) + ".");
    }

    return assets;
}

// ============================================================
// LOAD COVARIANCE MATRIX
// ============================================================

vector<vector<double>> loadCovarianceMatrix(
    const vector<Asset> &originalAssets)
{
    const string filename = "Covariance-Table 1.csv";

    ifstream file = openDataFile(filename);

    string line;

    // --------------------------------------------------------
    // Read header
    // --------------------------------------------------------

    if (!getline(file, line))
    {
        throw runtime_error("Covariance file is empty.");
    }

    vector<string> header = parseCSVLine(line);

    if (header.size() < 2)
    {
        throw runtime_error("Invalid covariance matrix header.");
    }

    // Map ticker -> column index in the numeric part of each row
    unordered_map<string, int> columnIndex;

    for (size_t i = 1; i < header.size(); ++i)
    {
        string ticker = trim(header[i]);

        if (!ticker.empty())
        {
            columnIndex[ticker] = static_cast<int>(i - 1);
        }
    }

    // --------------------------------------------------------
    // Read covariance rows
    // --------------------------------------------------------

    unordered_map<string, vector<double>> covarianceRows;

    while (getline(file, line))
    {
        if (trim(line).empty())
            continue;

        vector<string> fields = parseCSVLine(line);

        if (fields.size() < 2)
            continue;

        string rowTicker = trim(fields[0]);

        if (rowTicker.empty())
            continue;

        vector<double> values;

        for (size_t i = 1; i < fields.size(); ++i)
        {
            if (trim(fields[i]).empty())
                continue;

            try
            {
                values.push_back(stod(fields[i]));
            }
            catch (...)
            {
                throw runtime_error(
                    "Invalid covariance value in row: " + rowTicker);
            }
        }

        covarianceRows[rowTicker] = values;
    }

    file.close();

    // --------------------------------------------------------
    // Build matrix in the same order as originalAssets
    // --------------------------------------------------------

    int N = static_cast<int>(originalAssets.size());

    vector<vector<double>> covariance(
        N,
        vector<double>(N, 0.0));

    for (int i = 0; i < N; ++i)
    {
        const string &rowTicker = originalAssets[i].ticker;

        if (covarianceRows.find(rowTicker) == covarianceRows.end())
        {
            throw runtime_error(
                "Ticker " + rowTicker +
                " not found in covariance matrix.");
        }

        const vector<double> &row =
            covarianceRows[rowTicker];

        for (int j = 0; j < N; ++j)
        {
            const string &columnTicker =
                originalAssets[j].ticker;

            if (columnIndex.find(columnTicker) ==
                columnIndex.end())
            {
                throw runtime_error(
                    "Ticker " + columnTicker +
                    " not found in covariance header.");
            }

            int column = columnIndex[columnTicker];

            if (column >= static_cast<int>(row.size()))
            {
                throw runtime_error(
                    "Covariance row for " + rowTicker +
                    " does not contain enough values.");
            }

            covariance[i][j] = row[column];
        }
    }

    return covariance;
}

// ============================================================
// COUNT ALL FEASIBLE PORTFOLIOS
// ============================================================
//
// This is NOT used to solve the optimization.
//
// It tells us how large the original search space is:
//     xi ∈ {0,1,2,3,4}
//     sum(xi) = 20
//
// Useful for demonstrating how much B&B prunes.
// ============================================================

unsigned long long countFeasiblePortfolios(int N)
{
    vector<unsigned long long> dp(TOTAL_UNITS + 1, 0);

    dp[0] = 1;

    for (int asset = 0; asset < N; ++asset)
    {
        vector<unsigned long long> next(
            TOTAL_UNITS + 1,
            0);

        for (int current = 0;
             current <= TOTAL_UNITS;
             ++current)
        {
            if (dp[current] == 0)
                continue;

            for (int units = 0;
                 units <= MAX_UNITS_PER_ASSET;
                 ++units)
            {
                if (current + units <= TOTAL_UNITS)
                {
                    next[current + units] +=
                        dp[current];
                }
            }
        }

        dp = next;
    }

    return dp[TOTAL_UNITS];
}

// ============================================================
// BRANCH & BOUND SOLVER
// ============================================================

class BranchAndBoundSolver
{
private:
    int N;

    vector<Asset> assets;

    // Covariance matrix in B&B search order
    vector<vector<double>> covariance;

    // Current allocation in B&B search order
    vector<int> currentWeights;

    // Best allocation found in B&B search order
    vector<int> bestWeights;

    double bestReturn =
        -numeric_limits<double>::infinity();

    double bestRisk =
        numeric_limits<double>::infinity();

    SearchStats stats;

    // --------------------------------------------------------
    // upperBound[index][units]
    //
    // Maximum possible additional return that can be obtained
    // from assets index ... N-1 using exactly 'units' units.
    //
    // IMPORTANT:
    // Risk is intentionally ignored here.
    //
    // Therefore this is an OPTIMISTIC upper bound:
    //
    // actual feasible return <= upper bound
    //
    // If upper bound <= current best return,
    // this branch cannot improve the solution.
    // --------------------------------------------------------

    vector<vector<double>> upperBound;

    // --------------------------------------------------------
    // completionCount[index][units]
    //
    // Number of complete feasible portfolios possible from
    // this state.
    //
    // Used only for reporting how many candidate portfolios
    // were skipped by pruning.
    // --------------------------------------------------------

    vector<vector<unsigned long long>> completionCount;

    // ========================================================
    // PRECOMPUTE UPPER BOUND
    // ========================================================

    void buildUpperBoundTable()
    {
        upperBound.assign(
            N + 1,
            vector<double>(
                TOTAL_UNITS + 1,
                -numeric_limits<double>::infinity()));

        // With no assets remaining, only 0 units is possible.
        upperBound[N][0] = 0.0;

        for (int index = N - 1;
             index >= 0;
             --index)
        {
            for (int units = 0;
                 units <= TOTAL_UNITS;
                 ++units)
            {
                double best =
                    -numeric_limits<double>::infinity();

                int maxTake =
                    min(MAX_UNITS_PER_ASSET, units);

                for (int take = 0;
                     take <= maxTake;
                     ++take)
                {
                    double remainingBound =
                        upperBound[index + 1][units - take];

                    if (!isfinite(remainingBound))
                        continue;

                    double additionalReturn =
                        (take * UNIT_WEIGHT) *
                        assets[index].expectedReturn;

                    best = max(
                        best,
                        additionalReturn +
                            remainingBound);
                }

                upperBound[index][units] = best;
            }
        }
    }

    // ========================================================
    // PRECOMPUTE NUMBER OF COMPLETIONS
    // ========================================================

    void buildCompletionCountTable()
    {
        completionCount.assign(
            N + 1,
            vector<unsigned long long>(
                TOTAL_UNITS + 1,
                0));

        completionCount[N][0] = 1;

        for (int index = N - 1;
             index >= 0;
             --index)
        {
            for (int units = 0;
                 units <= TOTAL_UNITS;
                 ++units)
            {
                unsigned long long count = 0;

                int maxTake =
                    min(MAX_UNITS_PER_ASSET, units);

                for (int take = 0;
                     take <= maxTake;
                     ++take)
                {
                    count +=
                        completionCount[index + 1]
                                       [units - take];
                }

                completionCount[index][units] = count;
            }
        }
    }

    // ========================================================
    // BRANCH & BOUND RECURSION
    // ========================================================

    void search(
        int index,
        int remainingUnits,
        double currentReturn,
        double currentVariance)
    {
        stats.nodesVisited++;

        // ----------------------------------------------------
        // 1. FEASIBILITY PRUNING
        // ----------------------------------------------------

        if (remainingUnits < 0)
        {
            stats.feasibilityPruned++;
            return;
        }

        int assetsRemaining = N - index;

        if (remainingUnits >
            assetsRemaining * MAX_UNITS_PER_ASSET)
        {
            stats.feasibilityPruned++;
            return;
        }

        // ----------------------------------------------------
        // 2. BOUND PRUNING
        // ----------------------------------------------------

        double optimisticReturn =
            currentReturn +
            upperBound[index][remainingUnits];

        if (optimisticReturn <=
            bestReturn + EPSILON)
        {
            stats.boundPruned++;

            // This entire subtree contains this many possible
            // complete portfolios.
            stats.candidatePortfoliosPruned +=
                completionCount[index][remainingUnits];

            return;
        }

        // ----------------------------------------------------
        // 3. COMPLETE PORTFOLIO
        // ----------------------------------------------------

        if (index == N)
        {
            // At this point remainingUnits must be zero.
            if (remainingUnits != 0)
                return;

            stats.completePortfoliosEvaluated++;

            double variance =
                max(0.0, currentVariance);

            double risk = sqrt(variance);

            // Risk is checked ONLY for a complete portfolio.
            if (risk <= RMAX + EPSILON)
            {
                if (currentReturn >
                    bestReturn + EPSILON)
                {
                    bestReturn = currentReturn;
                    bestRisk = risk;
                    bestWeights = currentWeights;
                }
            }

            return;
        }

        // ----------------------------------------------------
        // 4. DETERMINE ALLOWED ALLOCATIONS
        // ----------------------------------------------------

        int minWeight = max(
            0,
            remainingUnits -
                MAX_UNITS_PER_ASSET *
                    (N - index - 1));

        int maxWeight = min(
            MAX_UNITS_PER_ASSET,
            remainingUnits);

        // ----------------------------------------------------
        // 5. TRY HIGHER ALLOCATIONS FIRST
        //
        // Assets are ordered by expected return, so trying
        // larger allocations first tends to find a strong
        // solution early.
        // ----------------------------------------------------

        for (int weight = maxWeight;
             weight >= minWeight;
             --weight)
        {
            currentWeights[index] = weight;

            double newWeight =
                weight * UNIT_WEIGHT;

            // ------------------------------------------------
            // Incremental return
            // ------------------------------------------------

            double newReturn =
                currentReturn +
                newWeight *
                    assets[index].expectedReturn;

            // ------------------------------------------------
            // Incremental portfolio variance
            //
            // New variance:
            //
            // Vnew = Vold
            //      + wi^2 * Cov(i,i)
            //      + wi*wj*Cov(i,j)
            //      + wj*wi*Cov(j,i)
            //
            // Since covariance is symmetric, this is normally
            // 2*wi*wj*Cov(i,j).
            //
            // We use both terms explicitly.
            // ------------------------------------------------

            double newVariance =
                currentVariance;

            newVariance +=
                newWeight *
                newWeight *
                covariance[index][index];

            for (int j = 0;
                 j < index;
                 ++j)
            {
                double previousWeight =
                    currentWeights[j] *
                    UNIT_WEIGHT;

                newVariance +=
                    newWeight *
                    previousWeight *
                    (covariance[index][j] +
                     covariance[j][index]);
            }

            search(
                index + 1,
                remainingUnits - weight,
                newReturn,
                newVariance);
        }

        currentWeights[index] = 0;
    }

public:
    // ========================================================
    // CONSTRUCTOR
    // ========================================================

    BranchAndBoundSolver(
        const vector<Asset> &inputAssets,
        const vector<vector<double>> &inputCovariance)
    {
        N = static_cast<int>(inputAssets.size());

        assets = inputAssets;

        // ----------------------------------------------------
        // Sort assets by expected return, highest first.
        //
        // This does NOT change the search space.
        // It only changes the order in which branches are
        // explored, helping B&B find a good incumbent early.
        // ----------------------------------------------------

        sort(
            assets.begin(),
            assets.end(),
            [](const Asset &a, const Asset &b)
            {
                if (a.expectedReturn !=
                    b.expectedReturn)
                {
                    return a.expectedReturn >
                           b.expectedReturn;
                }

                return a.originalIndex <
                       b.originalIndex;
            });

        // ----------------------------------------------------
        // Reorder covariance matrix to match B&B asset order.
        // ----------------------------------------------------

        covariance.assign(
            N,
            vector<double>(N, 0.0));

        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                covariance[i][j] =
                    inputCovariance
                        [assets[i].originalIndex]
                        [assets[j].originalIndex];
            }
        }

        currentWeights.assign(N, 0);
        bestWeights.assign(N, 0);

        // Build the B&B auxiliary tables.
        buildUpperBoundTable();
        buildCompletionCountTable();
    }

    // ========================================================
    // SOLVE
    // ========================================================

    Solution solve()
    {
        search(
            0,
            TOTAL_UNITS,
            0.0,
            0.0);

        Solution solution;

        solution.returnValue = bestReturn;
        solution.risk = bestRisk;
        solution.weights = bestWeights;

        return solution;
    }

    // ========================================================
    // GETTERS
    // ========================================================

    const SearchStats &getStats() const
    {
        return stats;
    }

    const vector<Asset> &getAssets() const
    {
        return assets;
    }

    int getN() const
    {
        return N;
    }
};

// ============================================================
// CALCULATE FINAL RETURN FROM ALLOCATION
// ============================================================

double calculatePortfolioReturn(
    const vector<int> &weights,
    const vector<Asset> &assets)
{
    double result = 0.0;

    for (size_t i = 0; i < weights.size(); ++i)
    {
        double weight =
            weights[i] * UNIT_WEIGHT;

        result +=
            weight *
            assets[i].expectedReturn;
    }

    return result;
}

// ============================================================
// CALCULATE FINAL RISK FROM ALLOCATION
// ============================================================
//
// This is used only as a final verification.
// The B&B search itself uses incremental variance.
// ============================================================

double calculatePortfolioRisk(
    const vector<int> &weights,
    const vector<Asset> &assets,
    const vector<vector<double>> &originalCovariance)
{
    int N = static_cast<int>(weights.size());

    vector<double> portfolioWeights(N, 0.0);

    for (int i = 0; i < N; ++i)
    {
        portfolioWeights[assets[i].originalIndex] = weights[i] * UNIT_WEIGHT;
    }

    double variance = 0.0;

    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
        {
            variance +=
                portfolioWeights[i] *
                portfolioWeights[j] *
                originalCovariance[i][j];
        }
    }

    return sqrt(max(0.0, variance));
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char *argv[])
{
    try
    {
        // ----------------------------------------------------
        // Default: all 20 assets.
        //
        // Optional:
        // ./bnb_seq 10
        //
        // This allows input-size scaling experiments.
        // ----------------------------------------------------

        int requestedN = 20;

        if (argc >= 2)
        {
            requestedN = stoi(argv[1]);
        }

        if (requestedN < 5 ||
            requestedN > 20)
        {
            cerr
                << "Error: N must be between 5 and 20."
                << endl;

            return 1;
        }

        // ====================================================
        // LOAD DATA
        // ====================================================

        vector<Asset> allAssets =
            loadOptimizationInput();

        vector<vector<double>> allCovariance =
            loadCovarianceMatrix(allAssets);

        // Select first N assets for scaling experiments.
        vector<Asset> selectedAssets(
            allAssets.begin(),
            allAssets.begin() + requestedN);

        // ----------------------------------------------------
        // Extract corresponding N x N covariance matrix
        // ----------------------------------------------------

        vector<vector<double>> selectedCovariance(
            requestedN,
            vector<double>(requestedN, 0.0));

        for (int i = 0; i < requestedN; ++i)
        {
            for (int j = 0; j < requestedN; ++j)
            {
                selectedCovariance[i][j] =
                    allCovariance[i][j];
            }
        }

        // ====================================================
        // DISPLAY BASIC INFORMATION
        // ====================================================

        cout << fixed << setprecision(10);

        cout << "\n";
        cout << "==============================================\n";
        cout << " Branch & Bound - Sequential\n";
        cout << "==============================================\n";

        cout << "Number of assets : "
             << requestedN << "\n";

        cout << "Allocation unit   : 5%\n";
        cout << "Total units       : "
             << TOTAL_UNITS << "\n";

        cout << "Max per asset     : 20%\n";

        cout << "Risk limit Rmax   : "
             << RMAX << "\n";

        cout << "==============================================\n";

        // ====================================================
        // DISPLAY DATA USED
        // ====================================================

        cout << "\nAssets loaded from Optimization_Input-Table 1.csv:\n";

        for (int i = 0; i < requestedN; ++i)
        {
            cout << "  "
                 << selectedAssets[i].ticker
                 << "  Expected Return = "
                 << selectedAssets[i].expectedReturn
                 << "\n";
        }

        // ====================================================
        // ORIGINAL SEARCH SPACE SIZE
        // ====================================================

        unsigned long long totalCandidates =
            countFeasiblePortfolios(requestedN);

        cout << "\nTotal feasible portfolios: "
             << totalCandidates
             << "\n";

        // ====================================================
        // RUN B&B
        // ====================================================
        //
        // Data loading is intentionally excluded from timing.
        //
        // The B&B solver's own setup and search are timed.
        // ====================================================

        auto startTime =
            chrono::high_resolution_clock::now();

        BranchAndBoundSolver solver(
            selectedAssets,
            selectedCovariance);

        Solution bestSolution =
            solver.solve();

        auto endTime =
            chrono::high_resolution_clock::now();

        chrono::duration<double> elapsed =
            endTime - startTime;

        // ====================================================
        // RESULTS
        // ====================================================

        const SearchStats &stats =
            solver.getStats();

        cout << "\n";
        cout << "==============================================\n";
        cout << " Branch & Bound Results\n";
        cout << "==============================================\n";

        cout << "Nodes visited              : "
             << stats.nodesVisited << "\n";

        cout << "Feasibility-pruned branches: "
             << stats.feasibilityPruned << "\n";

        cout << "Bound-pruned branches      : "
             << stats.boundPruned << "\n";

        cout << "Complete portfolios tested : "
             << stats.completePortfoliosEvaluated
             << "\n";

        cout << "Candidate portfolios pruned: "
             << stats.candidatePortfoliosPruned
             << "\n";

        // ----------------------------------------------------
        // Search-space reduction
        // ----------------------------------------------------

        double reduction = 0.0;

        if (totalCandidates > 0)
        {
            reduction =
                (static_cast<double>(
                     stats.candidatePortfoliosPruned) /
                 static_cast<double>(
                     totalCandidates)) *
                100.0;
        }

        cout << fixed << setprecision(4);

        cout << "Search-space reduction    : "
             << reduction
             << "%\n";

        // ----------------------------------------------------
        // Consistency check
        // ----------------------------------------------------

        unsigned long long accountedCandidates =
            stats.completePortfoliosEvaluated +
            stats.candidatePortfoliosPruned;

        cout << "\nCandidate accounting check:\n";

        cout << "  Expected candidates     : "
             << totalCandidates << "\n";

        cout << "  Evaluated + pruned      : "
             << accountedCandidates << "\n";

        if (accountedCandidates == totalCandidates)
        {
            cout << "  Status                  : PASS\n";
        }
        else
        {
            cout << "  Status                  : CHECK\n";
        }

        // ====================================================
        // BEST SOLUTION
        // ====================================================

        if (!isfinite(bestSolution.returnValue))
        {
            cout << "\nNo feasible portfolio satisfies Rmax = "
                 << RMAX << ".\n";

            cout << "\nExecution time: "
                 << elapsed.count()
                 << " seconds\n";

            return 0;
        }

        cout << "\n";
        cout << "==============================================\n";
        cout << " Optimal Portfolio\n";
        cout << "==============================================\n";

        cout << "Expected Return (daily): "
             << bestSolution.returnValue
             << "\n";

        cout << "Portfolio Risk (daily):  "
             << bestSolution.risk
             << "\n";

        cout << "Rmax:                    "
             << RMAX
             << "\n";

        // ====================================================
        // PORTFOLIO ALLOCATION
        //
        // Convert B&B sorted order back to original CSV order.
        // ====================================================

        vector<int> originalOrderWeights(
            requestedN,
            0);

        const vector<Asset> &searchAssets =
            solver.getAssets();

        for (int i = 0;
             i < requestedN;
             ++i)
        {
            originalOrderWeights[searchAssets[i].originalIndex] = bestSolution.weights[i];
        }

        cout << "\nPortfolio Allocation:\n";

        cout << left
             << setw(10) << "Ticker"
             << setw(15) << "Allocation"
             << "\n";

        cout << "----------------------------------------\n";

        for (int i = 0;
             i < requestedN;
             ++i)
        {
            if (originalOrderWeights[i] == 0)
                continue;

            double allocation =
                originalOrderWeights[i] *
                5.0;

            cout << left
                 << setw(10)
                 << selectedAssets[i].ticker
                 << setw(15)
                 << fixed
                 << setprecision(0)
                 << allocation
                 << "%\n";
        }

        // ====================================================
        // VERIFY TOTAL ALLOCATION
        // ====================================================

        int totalUnits = 0;

        for (int weight : originalOrderWeights)
        {
            totalUnits += weight;
        }

        cout << "\nTotal allocation units: "
             << totalUnits
             << "\n";

        cout << "Total allocation: "
             << totalUnits * 5
             << "%\n";

        // ====================================================
        // FINAL TIMING
        // ====================================================

        cout << "\n";
        cout << fixed << setprecision(4);
        cout << "Execution time: "
             << elapsed.count()
             << " seconds\n";

        cout << "==============================================\n";

        return 0;
    }
    catch (const exception &error)
    {
        cerr << "\nERROR: "
             << error.what()
             << "\n";

        return 1;
    }
}