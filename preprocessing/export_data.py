import pandas as pd
from pathlib import Path


# Project folders
PROJECT_ROOT = Path(__file__).resolve().parent.parent
INPUT_FILE = PROJECT_ROOT / "preprocessing" / "Portfolio_Optimization_Preprocessing-2.xlsx"
DATA_FOLDER = PROJECT_ROOT / "data"


# Create data folder if it does not exist
DATA_FOLDER.mkdir(exist_ok=True)


# --------------------------------------------------
# 1. Export expected returns
# --------------------------------------------------

optimization_input = pd.read_excel(
    INPUT_FILE,
    sheet_name="Optimization_Input"
)

expected_returns = optimization_input[
    ["Ticker", "Expected Return (Daily)"]
]

expected_returns.to_csv(
    DATA_FOLDER / "expected_returns.csv",
    index=False
)


# --------------------------------------------------
# 2. Export covariance matrix
# --------------------------------------------------

covariance = pd.read_excel(
    INPUT_FILE,
    sheet_name="Covariance"
)

covariance.to_csv(
    DATA_FOLDER / "covariance_matrix.csv",
    index=False
)


print("Data export completed successfully.")
print("Created:")
print(" - data/expected_returns.csv")
print(" - data/covariance_matrix.csv")