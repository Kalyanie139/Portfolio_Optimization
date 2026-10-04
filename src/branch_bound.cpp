#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <limits>

using namespace std;

// Stores all optimization data loaded from CSV files
struct PortfolioData
{
    vector<string> tickers;
    vector<double> expectedReturns;
    vector<vector<double>> covariance;
};

// Stores the current state of the Branch and Bound search
struct BBState
{
    vector<double> allocation;

    double currentReturn = 0.0;
    double allocatedWeight = 0.0;

    double bestReturn = -numeric_limits<double>::infinity();
    double bestRisk = 0.0;

    vector<double> bestAllocation;
};

// Load expected returns and tickers
PortfolioData loadPortfolioData(
    const string &inputFile,
    const string &covarianceFile)
{
    PortfolioData data;

    // -----------------------------
    // Load optimization_input.csv
    // -----------------------------
    ifstream input(inputFile);

    if (!input.is_open())
    {
        cerr << "Error: Could not open " << inputFile << endl;
        return data;
    }

    string line;

    // Skip header
    getline(input, line);

    while (getline(input, line))
    {
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

    // -----------------------------
    // Load covariance_matrix.csv
    // -----------------------------
    ifstream covarianceFileStream(covarianceFile);

    if (!covarianceFileStream.is_open())
    {
        cerr << "Error: Could not open "
             << covarianceFile << endl;
        return data;
    }

    // Skip header
    getline(covarianceFileStream, line);

    while (getline(covarianceFileStream, line))
    {
        if (line.empty())
            continue;

        stringstream ss(line);
        vector<double> row;

        // First value may be the ticker
        string value;
        getline(ss, value, ',');

        while (getline(ss, value, ','))
        {
            row.push_back(stod(value));
        }

        data.covariance.push_back(row);
    }

    covarianceFileStream.close();

    return data;
}

// Basic Branch and Bound setup
void branchAndBound(const PortfolioData &data)
{

    int n = data.expectedReturns.size();

    BBState state;

    // One allocation value for each asset
    state.allocation.assign(n, 0.0);

    state.bestAllocation.assign(n, 0.0);

    cout << "Branch and Bound setup" << endl;
    cout << "Number of assets: " << n << endl;

    cout << "Expected returns loaded: "
         << data.expectedReturns.size() << endl;

    cout << "Covariance matrix size: "
         << data.covariance.size()
         << " x ";

    if (!data.covariance.empty())
        cout << data.covariance[0].size();
    else
        cout << 0;

    cout << endl;
}

int main()
{

    PortfolioData data = loadPortfolioData(
        "data/Optimization_Input-Table 1.csv",
        "data/Covariance-Table 1.csv");

    // Basic validation
    if (data.tickers.empty() ||
        data.expectedReturns.empty() ||
        data.covariance.empty())
    {

        cerr << "Error: Portfolio data could not be loaded."
             << endl;

        return 1;
    }

    if (data.tickers.size() != data.expectedReturns.size())
    {
        cerr << "Error: Ticker and expected return counts do not match."
             << endl;

        return 1;
    }

    if (data.covariance.size() != data.expectedReturns.size())
    {
        cerr << "Error: Covariance matrix dimensions do not match "
             << "the number of assets." << endl;

        return 1;
    }

    branchAndBound(data);

    return 0;
}