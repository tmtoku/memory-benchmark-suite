import argparse

import humanize
import numpy as np
import pandas as pd


def format_bytes(bytes):
    return humanize.naturalsize(bytes, binary=True).replace(".0", "")


def load_benchmark_data(filename):
    df = pd.read_csv(filename)

    num_loads = df["NumLogicalLoads"]

    df["Latency"] = df["Cycles"] / num_loads

    df["L1DMissRate"] = (df["L1DMisses"] / num_loads) * 100
    df["L2MissRate"] = (df["L2Misses"] / num_loads) * 100
    df["L3MissRate"] = (df["L3Misses"] / num_loads) * 100
    df["L1TLBMissRate"] = (df["L1TLBMisses"] / num_loads) * 100
    df["L2TLBMissRate"] = (df["L2TLBMisses"] / num_loads) * 100

    df["PageEntries"] = np.ceil(df["BufferSize"] / df["PageSize"]).astype(int)

    return df


def print_table(title, df, columns, labels):
    FORMATTERS = {
        "BufferSize": format_bytes,
        "PageSize": format_bytes,
        "PageEntries": "{:.0f}".format,
        "Latency": "{:.2f}".format,
        "L1DMissRate": "{:.2f}".format,
        "L2MissRate": "{:.2f}".format,
        "L3MissRate": "{:.2f}".format,
        "L1TLBMissRate": "{:.2f}".format,
        "L2TLBMissRate": "{:.2f}".format,
    }

    table = df[columns].to_string(index=False, header=labels, formatters=FORMATTERS)

    if table:
        header_len = len(table.split("\n")[0])
    else:
        header_len = len(title)

    print(title)
    print("=" * header_len)
    print(table)
    print("\n")


def print_cache_latency_table(df):
    subset = df[df["PaddedElementSize"] == 64].copy()

    if subset.empty:
        return

    subset = subset.sort_values(by="BufferSize", ascending=True)

    title = "Cache Hierarchy Analysis (PaddedElementSize: 64 Bytes)"
    cols = ["BufferSize", "Latency", "L1DMissRate", "L2MissRate", "L3MissRate"]
    headers = [
        "BufferSize",
        "Latency (cycles)",
        "L1DMiss (%)",
        "L2Miss (%)",
        "L3Miss (%)",
    ]

    print_table(title, subset, cols, headers)


def print_tlb_latency_table(df):
    subset = df[df["PaddedElementSize"] == 4096].copy()

    if subset.empty:
        return

    subset = subset.sort_values(by=["BufferSize", "PageSize"], ascending=[True, False])

    title = "TLB Analysis (PaddedElementSize: 4096 Bytes)"
    cols = [
        "BufferSize",
        "PageSize",
        "PageEntries",
        "Latency",
        "L1TLBMissRate",
        "L2TLBMissRate",
    ]
    headers = [
        "BufferSize",
        "PageSize",
        "PageEntries",
        "Latency (cycles)",
        "L1TLBMiss (%)",
        "L2TLBMiss (%)",
    ]

    print_table(title, subset, cols, headers)


def main():
    parser = argparse.ArgumentParser(description="Analyze memory benchmark results.")
    parser.add_argument("filename", help="Path to the CSV file")
    args = parser.parse_args()

    df = load_benchmark_data(args.filename)
    print_cache_latency_table(df)
    print_tlb_latency_table(df)


if __name__ == "__main__":
    main()
