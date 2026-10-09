# Portfolio Optimization Using Branch and Bound and OpenMP

A parallel computing project that solves the portfolio optimization problem using **Exhaustive Search** and **Branch and Bound**, with OpenMP to improve execution time through parallel processing.

## Overview

Portfolio optimization aims to maximize expected portfolio return while keeping risk within a specified limit.

This project compares four implementations:

* Sequential Exhaustive Search
* Parallel Exhaustive Search using OpenMP
* Sequential Branch and Bound
* Parallel Branch and Bound using OpenMP

The implementations are evaluated based on execution time, scalability, and the effectiveness of parallelization.

## Problem Statement

The objective is to select an optimal allocation across 20 selected stocks from the S&P 500 historical dataset.

The optimization problem follows these constraints:

* Each stock can have a weight of `0%`, `5%`, `10%`, `15%`, or `20%`.
* The total portfolio allocation must equal `100%`.
* Portfolio risk must not exceed `0.016`.
* The objective is to maximize expected portfolio return.

## Dataset

The project uses historical S&P 500 stock market data.

* **Period:** January 2000 – February 2026
* **Dataset size:** Approximately 2.7 million records
* **Stocks:** Approximately 472
* **Price field:** Adjusted closing price (`Adj Close`)

The historical data is processed to calculate expected returns and the covariance matrix required for portfolio evaluation.

**Dataset source:** [S&P 500 Historical Analysis – Kaggle](https://www.kaggle.com/code/lalit7881/s-p-500-historical-analysis/input)

## Algorithms Implemented

### 1. Sequential Exhaustive Search

Evaluates all feasible portfolio allocations to identify the portfolio with the maximum expected return while satisfying the risk constraint.

### 2. Parallel Exhaustive Search

Uses OpenMP to distribute portfolio evaluations across multiple threads, reducing execution time compared with sequential exhaustive search.

### 3. Sequential Branch and Bound

Uses recursive search and bounding conditions to eliminate unpromising partial allocations, avoiding the need to evaluate every feasible portfolio.

### 4. Parallel Branch and Bound

Combines Branch and Bound with OpenMP to explore independent portions of the search space concurrently.

The parallel implementation divides the search into 25 top-level work units based on the allocation choices for the first two stocks and uses static scheduling.

## Tech Stack

* **Language:** C++
* **Parallel Programming:** OpenMP
* **Compilation:** GNU G++ with C++17 support
* **Build Tool:** GNU Make
* **Data Processing:** Python

## Project Structure

```text
Portfolio_Optimization/
├── data/                 # Dataset and processed input files
├── preprocessing/        # Data preprocessing scripts
├── src/                  # C++ implementations
├── output/               # Program output files
├── results_analysis/     # Performance analysis and results
├── Makefile              # Build configuration
├── .gitignore
└── README.md
```

## Getting Started

### Prerequisites

Make sure the following are installed:

* Git
* GNU G++ with C++17 support
* OpenMP
* GNU Make

### Clone the Repository

```bash
git clone https://github.com/karvevaishnavi16/Portfolio_Optimization.git
cd Portfolio_Optimization
```

### Build the Project

```bash
make CXX=g++-14
```

Use the compiler available on your system. If your compiler is already configured as `g++`, you can run `make` instead.

To remove generated executables and rebuild:

```bash
make clean
make CXX=g++-14
```

## Running the Programs

Run the implementations from the project root directory.

### Sequential Exhaustive Search

```bash
./sequential_exhaustive 20
```

### Parallel Exhaustive Search

```bash
./bf_openmp 20 16 0.016
```

### Sequential Branch and Bound

```bash
./branch_bound_seq 20
```

### Parallel Branch and Bound

```bash
OMP_NUM_THREADS=4 ./branch_bound_parallel 20
```

**Note:** The executable names and command-line arguments should match the actual targets and implementations defined in the `Makefile` and source code.

## Results

The implementations are compared using execution time and parallel performance.

The best portfolio reported by the project has:

* **Expected daily return:** `0.00146236`
* **Portfolio risk:** `0.01598461`
* **Total allocation:** `100%`

### Performance Highlights

* Sequential Exhaustive Search evaluated **35,561,166,195 feasible portfolios** in approximately **5577.67 seconds**.
* Parallel Branch and Bound took approximately **7.13 seconds** with one thread.
* Parallel Branch and Bound took approximately **2.61 seconds** with 16 threads.

These results demonstrate the potential performance improvement from pruning the search space and parallelizing the remaining work.

## Team

* Trupti Mete
* Tejal Udgave
* Vaishnavi Karve
* Kalyani Somvanshi

