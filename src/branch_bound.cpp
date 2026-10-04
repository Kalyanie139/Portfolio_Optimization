#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <limits>

using namespace std;

// Stores all optimization data loaded from CSV files
struct PortfolioData {
    vector<string> tickers;
    vector<double> expectedReturns;
    vector<vector<double>> covariance;
};

// Stores the current state of the Branch and Bound search
struct BBState {
    vector<double> allocation;

    double currentReturn = 0.0;
    double allocatedWeight = 0.0;

    double bestReturn = -numeric_limits<double>::infinity();
    double bestRisk = 0.0;

    vector<double> bestAllocation;
};

// Load expected returns and covariance matrix
PortfolioData loadPortfolioData(
    const string& inputFile,
    const string& covarianceFile
) {
    PortfolioData data;

    // Load optimization input
    ifstream input(inputFile);

    if (!input.is_open()) {
        cerr << "Error: Could not open " << inputFile << endl;
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

        getline(ss, ticker, ',');
        getline(ss, returnString, ',');

        data.tickers.push_back(ticker);
        data.expectedReturns.push_back(stod(returnString));
    }

    input.close();

    // Load covariance matrix
    ifstream covarianceFileStream(covarianceFile);

    if (!covarianceFileStream.is_open()) {
        cerr << "Error: Could not open "
             << covarianceFile << endl;
        return data;
    }

    // Skip header
    getline(covarianceFileStream, line);

    while (getline(covarianceFileStream, line)) {
        if (line.empty())
            continue;

        stringstream ss(line);
        vector<double> row;

        // First value is the ticker
        string value;
        getline(ss, value, ',');

        while (getline(ss, value, ',')) {
            row.push_back(stod(value));
        }

        data.covariance.push_back(row);
    }

    covarianceFileStream.close();

    return data;
}

// Branch and Bound search
void branchAndBound(
    const PortfolioData& data,
    BBState& state,
    int index
) {
    // All assets have been assigned an allocation
    if (index == static_cast<int>(data.expectedReturns.size())) {

        // A complete portfolio must use exactly 100%
        if (abs(state.allocatedWeight - 1.0) < 1e-9) {

            // For now, choose the portfolio
            // with the highest expected return
            if (state.currentReturn > state.bestReturn) {
                state.bestReturn = state.currentReturn;
                state.bestAllocation = state.allocation;
            }
        }

        return;
    }

    // Allowed allocation choices:
    // 0%, 5%, 10%, 15%, 20%
    const double choices[] = {
        0.00,
        0.05,
        0.10,
        0.15,
        0.20
    };

    for (double weight : choices) {

        // Do not allow allocation to exceed 100%
        if (state.allocatedWeight + weight > 1.0)
            continue;

        // Assign allocation to current asset
        state.allocation[index] = weight;

        // Update current state
        state.allocatedWeight += weight;

        state.currentReturn +=
            weight * data.expectedReturns[index];

        // Move to next asset
        branchAndBound(
            data,
            state,
            index + 1
        );

        // Undo changes before trying next choice
        state.currentReturn -=
            weight * data.expectedReturns[index];

        state.allocatedWeight -= weight;

        state.allocation[index] = 0.0;
    }
}

int main() {

    PortfolioData data = loadPortfolioData(
        "data/Optimization_Input-Table 1.csv",
        "data/Covariance-Table 1.csv"
    );

    // Basic validation
    if (data.tickers.empty() ||
        data.expectedReturns.empty() ||
        data.covariance.empty()) {

        cerr << "Error: Portfolio data could not be loaded."
             << endl;

        return 1;
    }

    if (data.tickers.size() != data.expectedReturns.size()) {
        cerr << "Error: Ticker and expected return counts do not match."
             << endl;

        return 1;
    }

    if (data.covariance.size() != data.expectedReturns.size()) {
        cerr << "Error: Covariance matrix dimensions do not match "
             << "the number of assets." << endl;

        return 1;
    }

    // Create initial Branch and Bound state
    BBState state;

    int n = data.expectedReturns.size();

    state.allocation.assign(n, 0.0);
    state.bestAllocation.assign(n, 0.0);

    // Start Branch and Bound from the first asset
    branchAndBound(data, state, 0);

    cout << "Number of assets: " << n << endl;

    cout << "Best return: "
         << state.bestReturn << endl;

    cout << "Best allocation:" << endl;

    for (int i = 0; i < n; i++) {
        if (state.bestAllocation[i] > 0.0) {
            cout << data.tickers[i]
                 << ": "
                 << state.bestAllocation[i] * 100
                 << "%" << endl;
        }
    }

    return 0;
}