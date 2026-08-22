import argparse

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib import ticker
from matplotlib.axes import Axes

plt.rcParams.update(
    {
        "font.family": "serif",
        "font.size": 9,
        "axes.labelsize": 9,
        "legend.fontsize": 7,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "lines.linewidth": 1.2,
        "lines.markersize": 1.5,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
    }
)

BENCHMARKS = (
    ("dividend", "Dividend width (bits)", "Dividend bit length", "Divisor"),
    ("quotient", "Quotient width (bits)", "Quotient bit length", "Dividend"),
)


def load_data(filename: str) -> pd.DataFrame:
    return pd.read_csv(filename)


def calculate_cycles_per_division(df: pd.DataFrame) -> pd.DataFrame:
    data = df.copy()
    if (data["NumIterations"] <= 0).any():
        raise ValueError("NumIterations must be positive")

    data["Latency"] = data["LatencyCycles"] / data["NumIterations"]
    data["DividerBusy"] = data["DividerBusyCycles"] / data["NumIterations"]
    data["Difference"] = data["DividerBusy"] - data["Latency"]
    if not np.isfinite(data[["Latency", "DividerBusy", "Difference"]]).all().all():
        raise ValueError("Derived values contain non-finite values")
    return data


def select_benchmark_data(df: pd.DataFrame, benchmark_name: str) -> pd.DataFrame:
    data = df[df["Benchmark"] == benchmark_name].sort_values("NumBits")
    if data.empty:
        raise ValueError(f"No data for the {benchmark_name} benchmark")
    return data


def get_column_unique_value(data: pd.DataFrame, column: str) -> int:
    values = data[column].unique()
    if len(values) != 1:
        raise ValueError(f"{column} must contain exactly one unique value")
    return int(values[0])


def format_power_of_two(value: int) -> str:
    if value == 1:
        return "1"
    if value > 0 and (value & (value - 1)) == 0:
        return rf"$2^{{{value.bit_length() - 1}}}$"
    return f"{value:,}"


def print_latency_tables(df: pd.DataFrame) -> None:
    columns = ["NumBits", "Latency", "DividerBusy", "Difference"]
    headers = ["Bits", "IDIV latency", "Divider busy", "Busy - latency"]
    formatters = {
        "Latency": "{:.2f}".format,
        "DividerBusy": "{:.2f}".format,
        "Difference": "{:.2f}".format,
    }

    for benchmark_name, table_title, _, _ in BENCHMARKS:
        data = select_benchmark_data(df, benchmark_name)
        table = data[columns].to_string(index=False, header=headers, formatters=formatters)
        print(table_title)
        print("=" * len(table.split("\n")[0]))
        print(table)
        print()


def plot_cycles(ax: Axes, data: pd.DataFrame, title: str, xlabel: str) -> None:
    ax.plot(
        data["NumBits"],
        data["Latency"],
        marker="o",
        label="IDIV latency",
    )
    ax.plot(
        data["NumBits"],
        data["DividerBusy"],
        marker="s",
        label="Divider busy cycles",
    )
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_xlim(0, 64)
    ax.xaxis.set_major_locator(ticker.MultipleLocator(10))
    ax.xaxis.set_minor_locator(ticker.MultipleLocator(2))
    ax.yaxis.set_major_locator(ticker.MultipleLocator(5))
    ax.yaxis.set_minor_locator(ticker.MultipleLocator(1))
    ax.grid(visible=True, which="major", alpha=0.30, linewidth=0.6)
    ax.grid(visible=True, which="minor", alpha=0.12, linewidth=0.4)
    ax.legend()


def plot_results(df: pd.DataFrame, output_filename: str) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(8, 4), sharey=True)

    for ax, (benchmark_name, _, xlabel, fixed_column) in zip(axes, BENCHMARKS, strict=True):
        data = select_benchmark_data(df, benchmark_name)
        fixed_value = format_power_of_two(get_column_unique_value(data, fixed_column))
        title = f"Different {benchmark_name} bit lengths\n({fixed_column.lower()} = {fixed_value})"
        plot_cycles(ax, data, title, xlabel)

    axes[0].set_ylabel("Cycles per division")
    fig.tight_layout()
    fig.savefig(output_filename, bbox_inches="tight")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("filename")
    parser.add_argument("--output", default="integer_division.pdf")
    args = parser.parse_args()

    df = calculate_cycles_per_division(load_data(args.filename))
    print_latency_tables(df)
    plot_results(df, args.output)
    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
