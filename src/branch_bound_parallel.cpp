#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <omp.h>

using namespace std;

// ------------------------------------------------------------
// CONSTANTS
// ------------------------------------------------------------

constexpr int TOTAL_UNITS = 20;            // 100% / 5%
constexpr int MAX_UNITS_PER_ASSET = 4;     // maximum 20%
constexpr double UNIT_WEIGHT = 1.0 / 20.0;
constexpr double RMAX = 0.016;
constexpr double EPSILON = 1e-12;


// ------------------------------------------------------------
// DATA STRUCTURES
// ------------------------------------------------------------

struct Asset
{
    string ticker;
    double expectedReturn;
    int originalIndex;
};

struct Solution
{
    double returnValue = -1e100;
    double risk = 0.0;
    vector<int> weights;
};


// ------------------------------------------------------------
// CSV HELPER
// ------------------------------------------------------------

vector<string> splitCSVLine(const string& line)
{
    vector<string> values;
    string value;
    stringstream ss(line);

    while (getline(ss, value, ','))
    {
        values.push_back(value);
    }

    return values;
}


// ------------------------------------------------------------
// LOAD EXPECTED RETURNS
// ------------------------------------------------------------

vector<Asset> loadOptimizationInput(
    const string& filename)
{
    ifstream file(filename);

    if (!file.is_open())
    {
        cerr << "Error opening " << filename << endl;
        exit(1);
    }

    string line;
    getline(file, line); // header

    vector<Asset> assets;

    while (getline(file, line))
    {
        if (line.empty())
            continue;

        vector<string> values =
            splitCSVLine(line);

        if (values.size() < 2)
            continue;

        Asset asset;

        asset.ticker = values[0];
        asset.expectedReturn = stod(values[1]);
        asset.originalIndex = assets.size();

        assets.push_back(asset);
    }

    file.close();

    return assets;
}


// ------------------------------------------------------------
// LOAD COVARIANCE MATRIX
// ------------------------------------------------------------

vector<vector<double>> loadCovarianceMatrix(
    const string& filename,
    const vector<Asset>& assets)
{
    ifstream file(filename);

    if (!file.is_open())
    {
        cerr << "Error opening " << filename << endl;
        exit(1);
    }

    string line;

    getline(file, line);

    vector<string> header =
        splitCSVLine(line);

    int n = assets.size();

    vector<vector<double>> covariance(
        n,
        vector<double>(n, 0.0)
    );


    // Find column corresponding to each ticker
    vector<int> columnIndex(n, -1);

    for (int i = 0; i < n; i++)
    {
        for (int j = 1; j < header.size(); j++)
        {
            if (header[j] == assets[i].ticker)
            {
                columnIndex[i] = j;
                break;
            }
        }
    }


    // Read rows
    while (getline(file, line))
    {
        if (line.empty())
            continue;

        vector<string> values =
            splitCSVLine(line);

        if (values.size() < 2)
            continue;

        string rowTicker = values[0];

        int row = -1;

        for (int i = 0; i < n; i++)
        {
            if (assets[i].ticker == rowTicker)
            {
                row = i;
                break;
            }
        }

        if (row == -1)
            continue;

        for (int col = 0; col < n; col++)
        {
            if (columnIndex[col] != -1 &&
                columnIndex[col] < values.size())
            {
                covariance[row][col] =
                    stod(values[columnIndex[col]]);
            }
        }
    }

    file.close();

    return covariance;
}


// ------------------------------------------------------------
// BRANCH AND BOUND SOLVER
// ------------------------------------------------------------

class BranchAndBoundSolver
{
private:

    int n;

    vector<Asset> assets;

    vector<vector<double>> covariance;

    vector<vector<double>> upperBound;


    // --------------------------------------------------------
    // BUILD RETURN UPPER BOUND
    // --------------------------------------------------------

    void buildUpperBound()
    {
        upperBound.assign(
            n + 1,
            vector<double>(
                TOTAL_UNITS + 1,
                -1e100
            )
        );

        upperBound[n][0] = 0.0;


        for (int index = n - 1;
             index >= 0;
             index--)
        {
            for (int units = 0;
                 units <= TOTAL_UNITS;
                 units++)
            {
                double bestValue = -1e100;

                for (int allocation = 0;
                     allocation <= MAX_UNITS_PER_ASSET;
                     allocation++)
                {
                    if (allocation > units)
                        break;

                    double remainingValue =
                        upperBound[index + 1]
                                   [units - allocation];

                    if (remainingValue <= -1e90)
                        continue;

                    double value =
                        allocation *
                        UNIT_WEIGHT *
                        assets[index].expectedReturn;

                    value += remainingValue;

                    bestValue =
                        max(bestValue, value);
                }

                upperBound[index][units] =
                    bestValue;
            }
        }
    }


    // --------------------------------------------------------
    // CALCULATE FINAL RISK
    // --------------------------------------------------------

    double calculateRisk(
        const vector<int>& weights)
    {
        double variance = 0.0;

        for (int i = 0; i < n; i++)
        {
            double wi =
                weights[i] * UNIT_WEIGHT;

            for (int j = 0; j < n; j++)
            {
                double wj =
                    weights[j] * UNIT_WEIGHT;

                variance +=
                    wi * wj *
                    covariance[i][j];
            }
        }

        return sqrt(max(0.0, variance));
    }


    // --------------------------------------------------------
    // UPDATE LOCAL BEST
    // --------------------------------------------------------

    void updateBest(
        Solution& localBest,
        const vector<int>& weights,
        double currentReturn,
        double currentVariance)
    {
        double risk =
            sqrt(max(0.0, currentVariance));

        if (risk <= RMAX &&
            currentReturn >
            localBest.returnValue + EPSILON)
        {
            localBest.returnValue =
                currentReturn;

            localBest.risk =
                risk;

            localBest.weights =
                weights;
        }
    }


    // --------------------------------------------------------
    // RECURSIVE B&B SEARCH
    //
    // This is the SAME search logic as sequential B&B.
    // The only difference is that best is passed locally.
    // --------------------------------------------------------

    void search(
        int index,
        int remainingUnits,
        double currentReturn,
        double currentVariance,
        vector<int>& weights,
        Solution& localBest)
    {
        // ----------------------------------------------------
        // 1. FEASIBILITY PRUNING
        // ----------------------------------------------------

        if (remainingUnits < 0)
            return;

        int remainingAssets =
            n - index;

        if (remainingUnits >
            remainingAssets *
            MAX_UNITS_PER_ASSET)
        {
            return;
        }


        // ----------------------------------------------------
        // 2. RETURN UPPER-BOUND PRUNING
        // ----------------------------------------------------

        double possibleReturn =
            currentReturn +
            upperBound[index][remainingUnits];

        if (possibleReturn <=
            localBest.returnValue +
            EPSILON)
        {
            return;
        }


        // ----------------------------------------------------
        // 3. LEAF NODE
        // ----------------------------------------------------

        if (index == n)
        {
            if (remainingUnits == 0)
            {
                updateBest(
                    localBest,
                    weights,
                    currentReturn,
                    currentVariance
                );
            }

            return;
        }


        // ----------------------------------------------------
        // 4. TRY HIGH RETURN ALLOCATIONS FIRST
        // ----------------------------------------------------

        for (int allocation =
                 MAX_UNITS_PER_ASSET;
             allocation >= 0;
             allocation--)
        {
            if (allocation > remainingUnits)
                continue;


            double newWeight =
                allocation *
                UNIT_WEIGHT;


            double newReturn =
                currentReturn +
                newWeight *
                assets[index].expectedReturn;


            // ------------------------------------------------
            // INCREMENTAL VARIANCE
            // ------------------------------------------------

            double newVariance =
                currentVariance +
                newWeight *
                newWeight *
                covariance[index][index];

            for (int j = 0;
                 j < index;
                 j++)
            {
                double previousWeight =
                    weights[j] *
                    UNIT_WEIGHT;

                newVariance +=
                    newWeight *
                    previousWeight *
                    (
                        covariance[index][j] +
                        covariance[j][index]
                    );
            }


            weights[index] =
                allocation;


            search(
                index + 1,
                remainingUnits - allocation,
                newReturn,
                newVariance,
                weights,
                localBest
            );


            weights[index] = 0;
        }
    }


public:

    // --------------------------------------------------------
    // CONSTRUCTOR
    // --------------------------------------------------------

    BranchAndBoundSolver(
        const vector<Asset>& inputAssets,
        const vector<vector<double>>& inputCovariance)
    {
        assets = inputAssets;
        covariance = inputCovariance;

        n = assets.size();


        // ----------------------------------------------------
        // SORT ASSETS ONCE
        // ----------------------------------------------------

        vector<int> order(n);

        for (int i = 0; i < n; i++)
        {
            order[i] = i;
        }

        sort(
            order.begin(),
            order.end(),
            [&](int a, int b)
            {
                return assets[a].expectedReturn >
                       assets[b].expectedReturn;
            }
        );


        // ----------------------------------------------------
        // REORDER ASSETS
        // ----------------------------------------------------

        vector<Asset> sortedAssets(n);

        for (int i = 0; i < n; i++)
        {
            sortedAssets[i] =
                assets[order[i]];
        }

        assets =
            sortedAssets;


        // ----------------------------------------------------
        // REORDER COVARIANCE MATRIX
        // ----------------------------------------------------

        vector<vector<double>>
            sortedCovariance(
                n,
                vector<double>(n, 0.0)
            );

        for (int i = 0; i < n; i++)
        {
            for (int j = 0; j < n; j++)
            {
                sortedCovariance[i][j] =
                    covariance[
                        order[i]
                    ][
                        order[j]
                    ];
            }
        }

        covariance =
            sortedCovariance;


        // ----------------------------------------------------
        // PRECOMPUTE BOUND
        // ----------------------------------------------------

        buildUpperBound();
    }


    // --------------------------------------------------------
    // PARALLEL SOLVE
    // --------------------------------------------------------

    Solution solveParallel()
    {
        Solution globalBest;

        globalBest.weights.assign(n, 0);


        /*
         * We parallelize the first TWO levels.
         *
         * First stock  : 5 choices
         * Second stock : 5 choices
         *
         * Maximum = 25 independent subtrees.
         *
         * This gives more parallel work than creating
         * only 5 tasks.
         */

        #pragma omp parallel for collapse(2) schedule(static)
        for (int firstAllocation = 0;
             firstAllocation <= MAX_UNITS_PER_ASSET;
             firstAllocation++)
        {
            for (int secondAllocation = 0;
                 secondAllocation <= MAX_UNITS_PER_ASSET;
                 secondAllocation++)
            {
                // --------------------------------------------
                // LOCAL SOLUTION FOR THIS SUBTREE
                // --------------------------------------------

                Solution localBest;

                localBest.weights.assign(n, 0);


                vector<int> weights(n, 0);

                weights[0] =
                    firstAllocation;

                weights[1] =
                    secondAllocation;


                int usedUnits =
                    firstAllocation +
                    secondAllocation;


                if (usedUnits > TOTAL_UNITS)
                    continue;


                // --------------------------------------------
                // CURRENT RETURN
                // --------------------------------------------

                double weight1 =
                    firstAllocation *
                    UNIT_WEIGHT;

                double weight2 =
                    secondAllocation *
                    UNIT_WEIGHT;

                double currentReturn =
                    weight1 *
                    assets[0].expectedReturn +
                    weight2 *
                    assets[1].expectedReturn;


                // --------------------------------------------
                // CURRENT VARIANCE
                // --------------------------------------------

                double currentVariance =
                    weight1 * weight1 *
                    covariance[0][0];

                currentVariance +=
                    weight2 * weight2 *
                    covariance[1][1];

                currentVariance +=
                    weight1 * weight2 *
                    (
                        covariance[0][1] +
                        covariance[1][0]
                    );


                // --------------------------------------------
                // SEARCH REMAINING TREE
                // --------------------------------------------

                search(
                    2,
                    TOTAL_UNITS -
                    usedUnits,
                    currentReturn,
                    currentVariance,
                    weights,
                    localBest
                );


                // --------------------------------------------
                // COMBINE THIS SUBTREE'S RESULT
                //
                // Only one final update per subtree.
                // Therefore critical section is tiny.
                // --------------------------------------------

                #pragma omp critical
                {
                    if (localBest.returnValue >
                        globalBest.returnValue +
                        EPSILON)
                    {
                        globalBest =
                            localBest;
                    }
                }
            }
        }


        return globalBest;
    }


    // --------------------------------------------------------
    // VERIFY RISK
    // --------------------------------------------------------

    double verifyRisk(
        const Solution& solution)
    {
        return calculateRisk(
            solution.weights
        );
    }


    // --------------------------------------------------------
    // PRINT RESULT
    // --------------------------------------------------------

    void printSolution(
        const Solution& solution)
    {
        cout << fixed
             << setprecision(8);

        cout << "\nBest Expected Return: "
             << solution.returnValue
             << endl;

        cout << "Risk: "
             << solution.risk
             << endl;

        cout << "\nPortfolio:\n";

        for (int i = 0; i < n; i++)
        {
            double allocation =
                solution.weights[i] *
                UNIT_WEIGHT *
                100.0;

            cout << setw(6)
                 << assets[i].ticker
                 << " : "
                 << setw(6)
                 << allocation
                 << "%\n";
        }
    }
};


// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------

int main(int argc, char* argv[])
{
    string optimizationFile =
        "data/Optimization_Input-Table 1.csv";

    string covarianceFile =
        "data/Covariance-Table 1.csv";


    // --------------------------------------------------------
    // LOAD ALL 20 STOCKS
    // --------------------------------------------------------

    vector<Asset> allAssets =
        loadOptimizationInput(
            optimizationFile
        );

    vector<vector<double>> allCovariance =
        loadCovarianceMatrix(
            covarianceFile,
            allAssets
        );


    // --------------------------------------------------------
    // INPUT SIZE
    //
    // ./branch_bound_parallel 20
    // --------------------------------------------------------

    int n = 20;

    if (argc >= 2)
    {
        n = stoi(argv[1]);
    }


    if (n < 2 || n > allAssets.size())
    {
        cerr << "N must be between 2 and "
             << allAssets.size()
             << endl;

        return 1;
    }


    // --------------------------------------------------------
    // SELECT FIRST N STOCKS
    // --------------------------------------------------------

    vector<Asset> assets(
        allAssets.begin(),
        allAssets.begin() + n
    );

    vector<vector<double>> covariance(
        n,
        vector<double>(n)
    );


    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            covariance[i][j] =
                allCovariance[i][j];
        }
    }


    // --------------------------------------------------------
    // CREATE SOLVER
    // --------------------------------------------------------

    BranchAndBoundSolver solver(
        assets,
        covariance
    );


    cout << "Problem Size: "
         << n
         << " stocks\n";

    cout << "OpenMP Threads: "
         << omp_get_max_threads()
         << "\n";


    // --------------------------------------------------------
    // START TIMER
    // --------------------------------------------------------

    double startTime =
        omp_get_wtime();


    // --------------------------------------------------------
    // SOLVE
    // --------------------------------------------------------

    Solution result =
        solver.solveParallel();


    // --------------------------------------------------------
    // STOP TIMER
    // --------------------------------------------------------

    double endTime =
        omp_get_wtime();


    // --------------------------------------------------------
    // PRINT RESULT
    // --------------------------------------------------------

    solver.printSolution(result);


    cout << "\nExecution Time: "
         << endTime - startTime
         << " seconds\n";


    // --------------------------------------------------------
    // VERIFY RISK
    // --------------------------------------------------------

    double verifiedRisk =
        solver.verifyRisk(result);

    cout << "Verified Risk: "
         << verifiedRisk
         << endl;


    if (verifiedRisk <= RMAX)
    {
        cout << "Risk Constraint: PASS\n";
    }
    else
    {
        cout << "Risk Constraint: FAIL\n";
    }


    // --------------------------------------------------------
    // VERIFY TOTAL ALLOCATION
    // --------------------------------------------------------

    int totalUnits = 0;

    for (int weight :
         result.weights)
    {
        totalUnits += weight;
    }


    cout << "Total Allocation: "
         << totalUnits * 5
         << "%\n";


    if (totalUnits == TOTAL_UNITS)
    {
        cout << "Allocation Constraint: PASS\n";
    }
    else
    {
        cout << "Allocation Constraint: FAIL\n";
    }


    return 0;
}