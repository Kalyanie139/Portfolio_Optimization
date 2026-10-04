#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>

using namespace std;

class PortfolioData
{
public:
    vector<string> tickers;
    vector<double> expectedReturns;
    vector<vector<double>> covariance;

    // Load expected returns from CSV
    void loadExpectedReturns(const string &filename)
    {
        ifstream file(filename);

        if (!file.is_open())
        {
            cout << "Error: Could not open "
                 << filename << endl;
            return;
        }

        string line;

        // Skip header
        getline(file, line);

        while (getline(file, line))
        {
            stringstream ss(line);

            string ticker;
            string returnValue;

            getline(ss, ticker, ',');
            getline(ss, returnValue, ',');

            tickers.push_back(ticker);
            expectedReturns.push_back(stod(returnValue));
        }

        file.close();
    }

    // Load covariance matrix from CSV
    void loadCovariance(const string &filename)
    {
        ifstream file(filename);

        if (!file.is_open())
        {
            cout << "Error: Could not open "
                 << filename << endl;
            return;
        }

        string line;

        // Skip header row
        getline(file, line);

        while (getline(file, line))
        {
            stringstream ss(line);

            string value;

            // Skip ticker at beginning of each row
            getline(ss, value, ',');

            vector<double> row;

            // Read covariance values
            while (getline(ss, value, ','))
            {
                row.push_back(stod(value));
            }

            covariance.push_back(row);
        }

        file.close();
    }

    // Validate loaded data
    bool validateData()
    {
        int n = tickers.size();

        if (n == 0)
        {
            cout << "Error: No assets found." << endl;
            return false;
        }

        // Current project dataset contains 20 assets
        if (n != 20)
        {
            cout << "Error: Expected 20 assets, found "
                 << n << endl;
            return false;
        }

        // Check expected returns
        if (expectedReturns.size() != n)
        {
            cout << "Error: Number of expected returns does not "
                 << "match number of assets." << endl;
            return false;
        }

        // Check covariance matrix rows
        if (covariance.size() != n)
        {
            cout << "Error: Covariance matrix should have "
                 << n << " rows." << endl;
            return false;
        }

        // Check covariance matrix columns
        for (int i = 0; i < n; i++)
        {
            if (covariance[i].size() != n)
            {
                cout << "Error: Covariance matrix row "
                     << i << " does not have "
                     << n << " values." << endl;

                return false;
            }
        }

        return true;
    }
};

// Calculate portfolio expected return
double calculateExpectedReturn(
    const vector<double> &allocation,
    const vector<double> &expectedReturns)
{
    double totalReturn = 0.0;

    for (int i = 0; i < allocation.size(); i++)
    {
        totalReturn +=
            allocation[i] * expectedReturns[i];
    }

    return totalReturn;
}

// Calculate portfolio risk
double calculatePortfolioRisk(
    const vector<double> &allocation,
    const vector<vector<double>> &covariance)
{
    double variance = 0.0;

    int n = allocation.size();

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            variance +=
                allocation[i] * allocation[j] * covariance[i][j];
        }
    }

    return sqrt(variance);
}

// Check whether allocation sums to 100%
bool isValidAllocation(
    const vector<double> &allocation)
{
    double totalAllocation = 0.0;

    for (int i = 0; i < allocation.size(); i++)
    {
        totalAllocation += allocation[i];
    }

    return abs(totalAllocation - 1.0) < 1e-9;
}

// Generate all possible portfolios
void generatePortfolios(
    int assetIndex,
    vector<double> &allocation,
    long long &portfolioCount,
    double &bestReturn,
    vector<double> &bestAllocation,
    const vector<double> &expectedReturns)
{
    int n = allocation.size();

    // Base case:
    // All assets have been assigned a weight
    if (assetIndex == n)
    {
        if (isValidAllocation(allocation))
        {
            portfolioCount++;

            double currentReturn =
                calculateExpectedReturn(
                    allocation,
                    expectedReturns);

            if (currentReturn > bestReturn)
            {
                bestReturn = currentReturn;
                bestAllocation = allocation;
            }
        }

        return;
    }
}


// Allowed allocation values
double allowedWeights[] =
    {
        0.00,
        0.05,
        0.10,
        0.15,
        0.20
    };

// Try every allowed weight for this asset
for (double weight : allowedWeights)
{
    allocation[assetIndex] = weight;

    generatePortfolios(
        assetIndex + 1,
        allocation,
        portfolioCount,
        bestReturn,
        bestAllocation,
        expectedReturns);
}
}

int main()
{
    PortfolioData data;

    // --------------------------------------------------
    // 1. Load expected returns
    // --------------------------------------------------

    data.loadExpectedReturns(
        "../data/expected_returns.csv");

    // --------------------------------------------------
    // 2. Load covariance matrix
    // --------------------------------------------------

    data.loadCovariance(
        "../data/covariance_matrix.csv");

    // --------------------------------------------------
    // 3. Validate input data
    // --------------------------------------------------

    if (!data.validateData())
    {
        cout << "Input data validation failed."
             << endl;

        return 1;
    }

    cout << "Input data validation successful."
         << endl;

    // --------------------------------------------------
    // 4. Test portfolio generation
    // --------------------------------------------------
    //
    // IMPORTANT:
    // Do NOT generate all portfolios for 20 assets yet.
    //
    // 20 assets -> 5^20 combinations
    // = 95,367,431,640,625
    //
    // Therefore, we first test the recursion
    // using only 3 assets.
    // --------------------------------------------------

    vector<double> testAllocation(3, 0.0);

    long long portfolioCount = 0;

    generatePortfolios(
        0,
        testAllocation,
        portfolioCount);

    cout << "\nTest portfolio generation"
         << endl;

    cout << "Number of portfolios generated: "
         << portfolioCount
         << endl;

    // --------------------------------------------------
    // 5. Example portfolio
    // --------------------------------------------------

    vector<double> allocation(
        data.tickers.size(),
        0.0);

    // Example allocation:
    // Asset 0 = 20%
    // Asset 1 = 10%
    // Asset 2 = 5%

    allocation[0] = 0.20;
    allocation[1] = 0.10;
    allocation[2] = 0.05;

    // --------------------------------------------------
    // 6. Check allocation validity
    // --------------------------------------------------

    if (isValidAllocation(allocation))
    {
        cout << "\nAllocation is valid."
             << endl;
    }
    else
    {
        cout << "\nAllocation is invalid."
             << endl;
    }

    // --------------------------------------------------
    // 7. Display example allocation
    // --------------------------------------------------

    cout << "\nExample Portfolio Allocation:"
         << endl;

    for (int i = 0;
         i < allocation.size();
         i++)
    {
        cout << data.tickers[i]
             << ": "
             << allocation[i] * 100
             << "%"
             << endl;
    }

    // --------------------------------------------------
    // 8. Calculate expected return
    // --------------------------------------------------

    double portfolioReturn =
        calculateExpectedReturn(
            allocation,
            data.expectedReturns);

    cout << "\nPortfolio Expected Daily Return: "
         << portfolioReturn
         << endl;

    // --------------------------------------------------
    // 9. Calculate portfolio risk
    // --------------------------------------------------

    double portfolioRisk =
        calculatePortfolioRisk(
            allocation,
            data.covariance);

    cout << "\nPortfolio Risk: "
         << portfolioRisk
         << endl;

    return 0;
}