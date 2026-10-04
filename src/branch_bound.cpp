#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <algorithm>
#include <omp.h>

using namespace std;

// ============================================================
// Constants
// ============================================================

const double ALLOCATION_STEP = 0.05;
const double MAX_ALLOCATION = 0.20;
const double MAX_RISK = 0.016;
const double TOTAL_ALLOCATION = 1.0;
const double EPS = 1e-9;

// ============================================================
// Portfolio data
// ============================================================

struct PortfolioData {
    vector<string> tickers;
    vector<double> expectedReturns;
    vector<vector<double>> covariance;
};

// ============================================================
// Best solution found so far
// ============================================================

struct BestSolution {
    double returnValue = -numeric_limits<double>::infinity();
    double risk = 0.0;
    vector<double> allocation;
};

// ============================================================
// Load CSV data
// ============================================================

PortfolioData loadPortfolioData(
    const string& inputFile,
    const string& covarianceFile
) {
    PortfolioData data;

    // --------------------------------------------------------
    // Load Optimization_Input-Table 1.csv
    // --------------------------------------------------------

    ifstream input(inputFile);

    if (!input.is_open()) {
        cerr << "Error: Could not open "
             << inputFile << endl;
        return data;
    }

    string line;

    // Skip header
    getline(input, line);

    while (getline(input, line)) {

        if (line.empty())
            continue;

        stringstream ss(line);

        string ticker;
        string returnString;

        // Column 1: Ticker
        getline(ss, ticker, ',');

        // Column 2: Expected Return (Daily)
        getline(ss, returnString, ',');

        if (ticker.empty() || returnString.empty())
            continue;

        data.tickers.push_back(ticker);
        data.expectedReturns.push_back(stod(returnString));
    }

    input.close();

    // --------------------------------------------------------
    // Load Covariance-Table 1.csv
    // --------------------------------------------------------

    ifstream covarianceInput(covarianceFile);

    if (!covarianceInput.is_open()) {
        cerr << "Error: Could not open "
             << covarianceFile << endl;
        return data;
    }

    // Header:
    // Ticker,TPL,AZO,MNST,...
    getline(covarianceInput, line);

    while (getline(covarianceInput, line)) {

        if (line.empty())
            continue;

        stringstream ss(line);

        vector<double> row;
        string value;

        // First column is ticker.
        getline(ss, value, ',');

        // Remaining columns are covariance values.
        while (getline(ss, value, ',')) {

            if (!value.empty())
                row.push_back(stod(value));
        }

        if (!row.empty())
            data.covariance.push_back(row);
    }

    covarianceInput.close();

    return data;
}

// ============================================================
// Calculate portfolio risk
//
// Risk = sqrt(w^T * Sigma * w)
// ============================================================

double calculateRisk(
    const PortfolioData& data,
    const vector<double>& allocation
) {
    int n = allocation.size();

    double variance = 0.0;

    for (int i = 0; i < n; ++i) {

        for (int j = 0; j < n; ++j) {

            variance +=
                allocation[i] *
                data.covariance[i][j] *
                allocation[j];
        }
    }

    // Protect against tiny negative values caused by
    // floating-point rounding.
    if (variance < 0.0 && variance > -EPS)
        variance = 0.0;

    return sqrt(variance);
}

// ============================================================
// Calculate current expected return
// ============================================================

double calculateReturn(
    const PortfolioData& data,
    const vector<double>& allocation
) {
    double result = 0.0;

    for (size_t i = 0; i < allocation.size(); ++i) {
        result +=
            allocation[i] *
            data.expectedReturns[i];
    }

    return result;
}

// ============================================================
// Optimistic return bound
//
// Assume all remaining allocation can be placed into the
// highest-return remaining assets, with at most 20% per asset.
//
// This is optimistic because it ignores the risk constraint.
// Therefore, if even this optimistic return cannot beat the
// current best feasible solution, the branch can safely prune.
// ============================================================

double calculateUpperBound(
    const PortfolioData& data,
    int index,
    double allocatedWeight,
    double currentReturn
) {
    double remainingWeight =
        TOTAL_ALLOCATION - allocatedWeight;

    if (remainingWeight <= EPS)
        return currentReturn;

    // Store remaining asset indices.
    vector<int> remainingIndices;

    for (int i = index;
         i < static_cast<int>(data.expectedReturns.size());
         ++i) {

        remainingIndices.push_back(i);
    }

    // Highest expected return first.
    sort(
        remainingIndices.begin(),
        remainingIndices.end(),
        [&](int a, int b) {
            return data.expectedReturns[a]
                 > data.expectedReturns[b];
        }
    );

    double upperBound = currentReturn;

    // Optimistically fill the remaining budget.
    for (int asset : remainingIndices) {

        if (remainingWeight <= EPS)
            break;

        double allocation =
            min(MAX_ALLOCATION, remainingWeight);

        upperBound +=
            allocation *
            data.expectedReturns[asset];

        remainingWeight -= allocation;
    }

    return upperBound;
}

// ============================================================
// Safely update global best solution
// ============================================================

void updateBestSolution(
    const PortfolioData& data,
    const vector<double>& allocation,
    double currentReturn,
    BestSolution& best
) {
    double risk =
        calculateRisk(data, allocation);

    // Risk constraint
    if (risk > MAX_RISK + EPS)
        return;

    // Maximize expected return
    #pragma omp critical(best_solution_update)
    {
        if (currentReturn > best.returnValue) {

            best.returnValue = currentReturn;
            best.risk = risk;
            best.allocation = allocation;
        }
    }
}

// ============================================================
// Recursive Branch and Bound
// ============================================================

void branchAndBoundRecursive(
    const PortfolioData& data,
    int index,
    vector<double>& allocation,
    double allocatedWeight,
    double currentReturn,
    BestSolution& best
) {
    int n = data.expectedReturns.size();

    // --------------------------------------------------------
    // Basic allocation feasibility
    // --------------------------------------------------------

    // Cannot allocate more than 100%.
    if (allocatedWeight > TOTAL_ALLOCATION + EPS)
        return;

    // --------------------------------------------------------
    // Can the remaining assets still fill the portfolio?
    //
    // Each remaining asset can contribute at most 20%.
    // --------------------------------------------------------

    int remainingAssets = n - index;

    double maximumRemainingWeight =
        remainingAssets * MAX_ALLOCATION;

    double requiredRemainingWeight =
        TOTAL_ALLOCATION - allocatedWeight;

    if (requiredRemainingWeight >
        maximumRemainingWeight + EPS) {

        return;
    }

    // --------------------------------------------------------
    // Complete portfolio
    // --------------------------------------------------------

    if (index == n) {

        if (abs(allocatedWeight -
                TOTAL_ALLOCATION) <= EPS) {

            updateBestSolution(
                data,
                allocation,
                currentReturn,
                best
            );
        }

        return;
    }

    // --------------------------------------------------------
    // Branch and Bound
    // --------------------------------------------------------

    double upperBound =
        calculateUpperBound(
            data,
            index,
            allocatedWeight,
            currentReturn
        );

    // Read the current best safely.
    double currentBest;

    #pragma omp critical(best_solution_update)
    {
        currentBest = best.returnValue;
    }

    // Even the optimistic return cannot beat the
    // current best feasible portfolio.
    if (upperBound <= currentBest + EPS)
        return;

    // --------------------------------------------------------
    // Branch
    //
    // Try higher allocations first so that good solutions
    // are found early and pruning becomes stronger.
    // --------------------------------------------------------

    const double choices[] = {
        0.20,
        0.15,
        0.10,
        0.05,
        0.00
    };

    for (double weight : choices) {

        // Do not exceed 100%.
        if (allocatedWeight +
            weight >
            TOTAL_ALLOCATION + EPS) {

            continue;
        }

        allocation[index] = weight;

        double newAllocatedWeight =
            allocatedWeight + weight;

        double newReturn =
            currentReturn +
            weight * data.expectedReturns[index];

        branchAndBoundRecursive(
            data,
            index + 1,
            allocation,
            newAllocatedWeight,
            newReturn,
            best
        );

        // Backtrack.
        allocation[index] = 0.0;
    }
}

// ============================================================
// Parallel Branch and Bound
//
// We create OpenMP tasks for the first two levels of the
// search tree. Each task independently searches its subtree.
// ============================================================

void branchAndBoundParallel(
    const PortfolioData& data,
    BestSolution& best
) {
    int n = data.expectedReturns.size();

    vector<double> rootAllocation(n, 0.0);

    const double choices[] = {
        0.20,
        0.15,
        0.10,
        0.05,
        0.00
    };

    #pragma omp parallel
    {
        #pragma omp single
        {

            // ------------------------------------------------
            // First level
            // ------------------------------------------------

            for (double weight1 : choices) {

                // Each first-level branch becomes an
                // independent OpenMP task.
                #pragma omp task firstprivate(weight1)
                {
                    vector<double> allocation(n, 0.0);

                    allocation[0] = weight1;

                    double allocatedWeight =
                        weight1;

                    double currentReturn =
                        weight1 *
                        data.expectedReturns[0];

                    // ----------------------------------------
                    // Second level
                    // ----------------------------------------

                    if (n > 1) {

                        for (double weight2 : choices) {

                            if (allocatedWeight +
                                weight2 >
                                TOTAL_ALLOCATION + EPS) {

                                continue;
                            }

                            vector<double> childAllocation =
                                allocation;

                            childAllocation[1] =
                                weight2;

                            double childWeight =
                                allocatedWeight +
                                weight2;

                            double childReturn =
                                currentReturn +
                                weight2 *
                                data.expectedReturns[1];

                            // Search the remainder
                            // sequentially inside this task.
                            branchAndBoundRecursive(
                                data,
                                2,
                                childAllocation,
                                childWeight,
                                childReturn,
                                best
                            );
                        }

                    } else {

                        branchAndBoundRecursive(
                            data,
                            1,
                            allocation,
                            allocatedWeight,
                            currentReturn,
                            best
                        );
                    }
                }
            }

            #pragma omp taskwait
        }
    }
}

// ============================================================
// Main
// ============================================================

int main() {

    // --------------------------------------------------------
    // Load actual project CSV files
    // --------------------------------------------------------

    PortfolioData data =
        loadPortfolioData(
            "data/Optimization_Input-Table 1.csv",
            "data/Covariance-Table 1.csv"
        );

    // --------------------------------------------------------
    // Validate input
    // --------------------------------------------------------

    if (data.tickers.empty() ||
        data.expectedReturns.empty() ||
        data.covariance.empty()) {

        cerr << "Error: Portfolio data could not be loaded."
             << endl;

        return 1;
    }

    int n = data.expectedReturns.size();

    if (data.tickers.size() !=
        data.expectedReturns.size()) {

        cerr << "Error: Ticker count and expected-return "
             << "count do not match." << endl;

        return 1;
    }

    if (data.covariance.size() != n) {

        cerr << "Error: Covariance matrix row count does "
             << "not match number of assets." << endl;

        return 1;
    }

    for (const auto& row : data.covariance) {

        if (row.size() != static_cast<size_t>(n)) {

            cerr << "Error: Covariance matrix is not "
                 << "square." << endl;

            return 1;
        }
    }

    // --------------------------------------------------------
    // Initialize best solution
    // --------------------------------------------------------

    BestSolution best;

    best.allocation.assign(n, 0.0);

    // --------------------------------------------------------
    // Run parallel Branch and Bound
    // --------------------------------------------------------

    double startTime =
        omp_get_wtime();

    branchAndBoundParallel(
        data,
        best
    );

    double endTime =
        omp_get_wtime();

    // --------------------------------------------------------
    // Output result
    // --------------------------------------------------------

    if (best.returnValue ==
        -numeric_limits<double>::infinity()) {

        cout << "No feasible portfolio found."
             << endl;

        return 0;
    }

    cout << "\n===== Branch and Bound + OpenMP =====\n";

    cout << "Number of assets: "
         << n << endl;

    cout << "Best Expected Return (Daily): "
         << best.returnValue << endl;

    cout << "Portfolio Risk (Daily): "
         << best.risk << endl;

    cout << "Risk Limit: "
         << MAX_RISK << endl;

    cout << "\nAllocation:\n";

    for (int i = 0; i < n; ++i) {

        if (best.allocation[i] > EPS) {

            cout << data.tickers[i]
                 << " : "
                 << best.allocation[i] * 100.0
                 << "%\n";
        }
    }

    cout << "\nExecution Time: "
         << (endTime - startTime)
         << " seconds\n";

    cout << "OpenMP Threads: "
         << omp_get_max_threads()
         << endl;

    return 0;
}