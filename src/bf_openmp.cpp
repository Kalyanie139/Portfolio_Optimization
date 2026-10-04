#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <omp.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

// ============================================================
// Exact Exhaustive Portfolio Optimization with OpenMP
//
// Input files (read from the project's data/ directory):
//   data/Optimization_Input-Table 1.csv
//   data/Covariance-Table 1.csv
//
// Each stock weight is one of 0%, 5%, 10%, 15%, 20%.
// Total allocation = 100%.
// Objective = maximize expected daily return subject to risk <= Rmax.
//
// Usage:
//   ./bruteforce_openmp
//   ./bruteforce_openmp <number_of_assets> <number_of_threads>
//   ./bruteforce_openmp <number_of_assets> <number_of_threads> <Rmax>
//
// Examples:
//   ./bruteforce_openmp 20 16
//   ./bruteforce_openmp 14 8
// ============================================================

constexpr int TOTAL_UNITS = 20;          // 100% / 5%
constexpr int MAX_UNITS_PER_ASSET = 4;   // 20% maximum per asset
constexpr double UNIT_WEIGHT = 1.0 / 20.0;
constexpr double DEFAULT_RMAX = 0.016;
constexpr double EPSILON = 1e-12;

struct Asset {
    string ticker;
    double expectedReturn;
};

struct Solution {
    double returnValue = -numeric_limits<double>::infinity();
    double risk = numeric_limits<double>::infinity();
    vector<int> weights;
};

// ------------------------------------------------------------
// CSV helpers
// ------------------------------------------------------------

string trim(const string& input) {
    size_t start = input.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";

    size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

vector<string> parseCSVLine(const string& line) {
    vector<string> fields;
    string current;
    bool insideQuotes = false;

    for (char c : line) {
        if (c == '"') {
            insideQuotes = !insideQuotes;
        } else if (c == ',' && !insideQuotes) {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }

    fields.push_back(trim(current));
    return fields;
}

// Supports running the executable from the project root or from src/.
ifstream openDataFile(const string& filename) {
    const vector<string> possiblePaths = {
        "data/" + filename,
        "../data/" + filename,
        filename
    };

    for (const string& path : possiblePaths) {
        ifstream file(path);
        if (file.is_open()) return file;
    }

    throw runtime_error(
        "Could not open data file: " + filename +
        "\nRun the executable from the project root or from src/."
    );
}

// ------------------------------------------------------------
// Load expected returns from Optimization_Input-Table 1.csv
// ------------------------------------------------------------

vector<Asset> loadOptimizationInput() {
    const string filename = "Optimization_Input-Table 1.csv";
    ifstream file = openDataFile(filename);

    string line;
    if (!getline(file, line)) {
        throw runtime_error("Optimization input file is empty.");
    }

    vector<Asset> assets;

    while (getline(file, line)) {
        if (trim(line).empty()) continue;

        vector<string> fields = parseCSVLine(line);
        if (fields.size() < 2) continue;

        string ticker = trim(fields[0]);
        if (ticker.empty() || ticker == "Ticker") continue;

        try {
            Asset asset;
            asset.ticker = ticker;
            asset.expectedReturn = stod(fields[1]);
            assets.push_back(asset);
        } catch (...) {
            throw runtime_error(
                "Invalid expected return for ticker: " + ticker);
        }
    }

    if (assets.empty()) {
        throw runtime_error("No assets found in Optimization_Input-Table 1.csv.");
    }

    return assets;
}

// ------------------------------------------------------------
// Load covariance matrix and reorder it to match the asset list
// ------------------------------------------------------------

vector<vector<double>> loadCovarianceMatrix(const vector<Asset>& assets) {
    const string filename = "Covariance-Table 1.csv";
    ifstream file = openDataFile(filename);

    string line;
    if (!getline(file, line)) {
        throw runtime_error("Covariance file is empty.");
    }

    vector<string> header = parseCSVLine(line);
    if (header.size() < 2) {
        throw runtime_error("Invalid covariance matrix header.");
    }

    unordered_map<string, int> columnIndex;
    for (size_t i = 1; i < header.size(); ++i) {
        string ticker = trim(header[i]);
        if (!ticker.empty()) {
            columnIndex[ticker] = static_cast<int>(i - 1);
        }
    }

    unordered_map<string, vector<double>> covarianceRows;

    while (getline(file, line)) {
        if (trim(line).empty()) continue;

        vector<string> fields = parseCSVLine(line);
        if (fields.size() < 2) continue;

        string rowTicker = trim(fields[0]);
        if (rowTicker.empty()) continue;

        vector<double> values;
        for (size_t i = 1; i < fields.size(); ++i) {
            string value = trim(fields[i]);
            if (value.empty()) continue;

            try {
                values.push_back(stod(value));
            } catch (...) {
                throw runtime_error(
                    "Invalid covariance value in row: " + rowTicker);
            }
        }

        covarianceRows[rowTicker] = values;
    }

    const int n = static_cast<int>(assets.size());
    vector<vector<double>> covariance(n, vector<double>(n, 0.0));

    for (int i = 0; i < n; ++i) {
        const string& rowTicker = assets[i].ticker;

        auto rowIt = covarianceRows.find(rowTicker);
        if (rowIt == covarianceRows.end()) {
            throw runtime_error(
                "Ticker " + rowTicker +
                " not found in covariance matrix.");
        }

        const vector<double>& row = rowIt->second;

        for (int j = 0; j < n; ++j) {
            auto colIt = columnIndex.find(assets[j].ticker);
            if (colIt == columnIndex.end()) {
                throw runtime_error(
                    "Ticker " + assets[j].ticker +
                    " not found in covariance header.");
            }

            const int column = colIt->second;
            if (column < 0 || column >= static_cast<int>(row.size())) {
                throw runtime_error(
                    "Covariance row for " + rowTicker +
                    " does not contain the required column.");
            }

            covariance[i][j] = row[column];
        }
    }

    return covariance;
}

// ------------------------------------------------------------
// Exact count of feasible portfolios for reporting/verification
// ------------------------------------------------------------

unsigned long long countFeasiblePortfolios(int n) {
    vector<unsigned long long> dp(TOTAL_UNITS + 1, 0);
    dp[0] = 1;

    for (int asset = 0; asset < n; ++asset) {
        vector<unsigned long long> next(TOTAL_UNITS + 1, 0);

        for (int current = 0; current <= TOTAL_UNITS; ++current) {
            if (dp[current] == 0) continue;

            for (int units = 0; units <= MAX_UNITS_PER_ASSET; ++units) {
                if (current + units <= TOTAL_UNITS) {
                    next[current + units] += dp[current];
                }
            }
        }

        dp.swap(next);
    }

    return dp[TOTAL_UNITS];
}

// ------------------------------------------------------------
// Exact exhaustive recursive search
// ------------------------------------------------------------

void search(
    int index,
    int n,
    int remainingUnits,
    vector<int>& weights,
    const vector<Asset>& assets,
    const vector<vector<double>>& covariance,
    double rmax,
    double partialReturn,
    double partialVariance,
    Solution& localBest,
    unsigned long long& evaluated
) {
    if (remainingUnits < 0) return;

    // Last stock gets exactly the remaining allocation.
    if (index == n - 1) {
        if (remainingUnits < 0 || remainingUnits > MAX_UNITS_PER_ASSET) {
            return;
        }

        weights[index] = remainingUnits;
        const double w = remainingUnits * UNIT_WEIGHT;

        double variance = partialVariance;

        for (int j = 0; j < index; ++j) {
            const double wj = weights[j] * UNIT_WEIGHT;
            variance += 2.0 * w * wj * covariance[index][j];
        }

        variance += w * w * covariance[index][index];

        const double risk = sqrt(max(0.0, variance));
        ++evaluated;

        if (risk <= rmax + EPSILON) {
            const double portfolioReturn =
                partialReturn + w * assets[index].expectedReturn;

            if (portfolioReturn > localBest.returnValue) {
                localBest.returnValue = portfolioReturn;
                localBest.risk = risk;
                localBest.weights = weights;
            }
        }

        return;
    }

    const int stocksAfter = n - index - 1;

    // Only generate weights that can still lead to a complete 100% portfolio.
    const int minUnits = max(
        0,
        remainingUnits - MAX_UNITS_PER_ASSET * stocksAfter
    );

    const int maxUnits = min(
        MAX_UNITS_PER_ASSET,
        remainingUnits
    );

    for (int units = minUnits; units <= maxUnits; ++units) {
        weights[index] = units;

        const double w = units * UNIT_WEIGHT;

        const double newReturn =
            partialReturn + w * assets[index].expectedReturn;

        double newVariance = partialVariance;

        for (int j = 0; j < index; ++j) {
            const double wj = weights[j] * UNIT_WEIGHT;
            newVariance += 2.0 * w * wj * covariance[index][j];
        }

        newVariance += w * w * covariance[index][index];

        search(
            index + 1,
            n,
            remainingUnits - units,
            weights,
            assets,
            covariance,
            rmax,
            newReturn,
            newVariance,
            localBest,
            evaluated
        );
    }
}

// ------------------------------------------------------------
// Parallel exhaustive search
// ------------------------------------------------------------

Solution parallelExhaustiveSearch(
    int n,
    const vector<Asset>& assets,
    const vector<vector<double>>& covariance,
    double rmax,
    unsigned long long& totalEvaluated
) {
    Solution globalBest;
    totalEvaluated = 0;

    #pragma omp parallel
    {
        Solution localBest;
        unsigned long long localEvaluated = 0;
        vector<int> weights(n, 0);

        // 25 independent first-two-stock branches.
        #pragma omp for schedule(static)
        for (int branch = 0; branch < 25; ++branch) {
            const int firstUnits = branch / 5;
            const int secondUnits = branch % 5;

            if (firstUnits + secondUnits > TOTAL_UNITS) continue;

            const int remainingUnits =
                TOTAL_UNITS - firstUnits - secondUnits;

            if (remainingUnits >
                MAX_UNITS_PER_ASSET * (n - 2)) {
                continue;
            }

            weights[0] = firstUnits;
            weights[1] = secondUnits;

            const double w0 = firstUnits * UNIT_WEIGHT;
            const double w1 = secondUnits * UNIT_WEIGHT;

            const double partialReturn =
                w0 * assets[0].expectedReturn +
                w1 * assets[1].expectedReturn;

            const double partialVariance =
                w0 * w0 * covariance[0][0] +
                w1 * w1 * covariance[1][1] +
                2.0 * w0 * w1 * covariance[0][1];

            search(
                2,
                n,
                remainingUnits,
                weights,
                assets,
                covariance,
                rmax,
                partialReturn,
                partialVariance,
                localBest,
                localEvaluated
            );
        }

        #pragma omp atomic
        totalEvaluated += localEvaluated;

        #pragma omp critical(best_solution)
        {
            if (localBest.returnValue > globalBest.returnValue) {
                globalBest = localBest;
            }
        }
    }

    return globalBest;
}

int main(int argc, char* argv[]) {
    try {
        int requestedAssets = 20;
        int requestedThreads = 0;
        double rmax = DEFAULT_RMAX;

        if (argc >= 2) requestedAssets = atoi(argv[1]);
        if (argc >= 3) requestedThreads = atoi(argv[2]);
        if (argc >= 4) rmax = atof(argv[3]);

        if (requestedThreads > 0) {
            omp_set_num_threads(requestedThreads);
        }

        vector<Asset> allAssets = loadOptimizationInput();

        if (requestedAssets < 2 ||
            requestedAssets > static_cast<int>(allAssets.size())) {
            throw runtime_error(
                "Number of assets must be between 2 and " +
                to_string(allAssets.size()) + ".");
        }

        // Use the first N assets from the CSV in its original order.
        vector<Asset> assets(
            allAssets.begin(),
            allAssets.begin() + requestedAssets
        );

        vector<vector<double>> allCovariance =
            loadCovarianceMatrix(allAssets);

        // Slice/reorder covariance to match the selected first N assets.
        vector<vector<double>> covariance(
            requestedAssets,
            vector<double>(requestedAssets, 0.0)
        );

        for (int i = 0; i < requestedAssets; ++i) {
            for (int j = 0; j < requestedAssets; ++j) {
                covariance[i][j] = allCovariance[i][j];
            }
        }

        const unsigned long long expected =
            countFeasiblePortfolios(requestedAssets);

        const int actualThreads =
            (requestedThreads > 0)
                ? requestedThreads
                : omp_get_max_threads();

        cout << "=============================================\n";
        cout << " Exact Exhaustive OpenMP Portfolio Optimizer\n";
        cout << "=============================================\n\n";
        cout << "Data source: data/ CSV files\n";
        cout << "Number of stocks: " << requestedAssets << "\n";
        cout << "Weight increment: 5%\n";
        cout << "Total allocation: 100%\n";
        cout << fixed << setprecision(10);
        cout << "Risk limit (daily): " << rmax << "\n";
        cout << "OpenMP threads: " << actualThreads << "\n\n";
        cout << "Expected feasible portfolios: " << expected << "\n\n";

        unsigned long long evaluated = 0;

        const auto start = chrono::high_resolution_clock::now();

        Solution best = parallelExhaustiveSearch(
            requestedAssets,
            assets,
            covariance,
            rmax,
            evaluated
        );

        const auto end = chrono::high_resolution_clock::now();

        const double elapsed =
            chrono::duration<double>(end - start).count();

        cout << "Evaluated portfolios: " << evaluated << "\n";
        cout << "Exhaustive check: "
             << (evaluated == expected ? "PASS" : "FAIL")
             << "\n\n";

        if (best.weights.empty()) {
            cout << "No feasible portfolio satisfies the risk constraint.\n";
        } else {
            cout << "Best Portfolio:\n";

            double totalWeight = 0.0;

            for (int i = 0; i < requestedAssets; ++i) {
                const double weight = best.weights[i] * 5.0;
                totalWeight += weight;

                cout << setw(6) << assets[i].ticker
                     << " : " << weight << "%\n";
            }

            cout << "\nTotal Weight: " << totalWeight << "%\n";
            cout << "Best Expected Return (daily): "
                 << best.returnValue << "\n";
            cout << "Portfolio Risk (daily): "
                 << best.risk << "\n";
            cout << "Risk Limit (daily): " << rmax << "\n";
        }

        cout << "\nExecution Time: " << elapsed << " seconds\n";

        return (evaluated == expected) ? 0 : 2;
    }
    catch (const exception& ex) {
        cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}