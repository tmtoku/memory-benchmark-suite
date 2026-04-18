import argparse

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.axes import Axes

from plot_utils import (
    CACHE_LINE_SIZE,
    apply_grid,
    apply_log2_xaxis,
    apply_log2_yaxis,
    apply_percent_yaxis,
    format_bytes,
    parse_size,
    plot_cache_boundaries,
    setup_rcparams,
)

setup_rcparams()

MISS_RATE_THRESHOLD = 1e-4

COLORS = {
    "latency": "#000000",
    "L1_hit": "#009E73",
    "L2_hit": "#56B4E9",
    "L3_hit": "#E69F00",
    "DRAM": "#D55E00",
}


def load_data(filename: str) -> pd.DataFrame:
    df = pd.read_csv(filename)
    num_loads = df["NumLogicalLoads"]
    df["Latency"] = df["Cycles"] / num_loads
    df["L1_miss"] = (df["L1DMisses"] / num_loads).clip(0, 1)
    df["L2_miss"] = (df["L2Misses"] / num_loads).clip(0, 1)
    df["L3_miss"] = (df["L3Misses"] / num_loads).clip(0, 1)
    df["L1_hit"] = (1 - df["L1_miss"]).clip(0, 1)
    df["L2_hit"] = (df["L1_miss"] - df["L2_miss"]).clip(0, 1)
    df["L3_hit"] = (df["L2_miss"] - df["L3_miss"]).clip(0, 1)
    return df


def estimate_cache_latency(df: pd.DataFrame) -> tuple[float, float, float]:
    d = df[df["PaddedElementSize"] == CACHE_LINE_SIZE].sort_values("BufferSize")

    # L1-dominant region: L1 miss rate is negligible
    # observed latency ~ L1_lat
    l1_region = d["L1_miss"] < MISS_RATE_THRESHOLD
    l1_latency = d[l1_region]["Latency"].median()

    # L2-dominant region: L1 mostly misses, L2 mostly hits.
    # observed latency ~ L1_hit * L1_lat + L2_hit * L2_lat
    l2_region = (d["L1_miss"] >= MISS_RATE_THRESHOLD) & (d["L2_miss"] < MISS_RATE_THRESHOLD)
    l2_latency = (
        (d[l2_region]["Latency"] - d[l2_region]["L1_hit"] * l1_latency) / d[l2_region]["L2_hit"]
    ).median()

    # L3-dominant region: L2 mostly misses, L3 mostly hits.
    # observed latency ~ L1_hit * L1_lat + L2_hit * L2_lat + L3_hit * L3_lat
    l3_region = (d["L2_miss"] >= MISS_RATE_THRESHOLD) & (d["L3_miss"] < MISS_RATE_THRESHOLD)
    l3_latency = (
        (
            d[l3_region]["Latency"]
            - d[l3_region]["L1_hit"] * l1_latency
            - d[l3_region]["L2_hit"] * l2_latency
        )
        / d[l3_region]["L3_hit"]
    ).median()

    return l1_latency, l2_latency, l3_latency


def print_cache_table(
    df: pd.DataFrame, l1_latency: float, l2_latency: float, l3_latency: float
) -> None:
    d = df[df["PaddedElementSize"] == CACHE_LINE_SIZE].sort_values("BufferSize")

    cols = ["BufferSize", "Latency", "L1_miss", "L2_miss", "L3_miss"]
    headers = ["BufferSize", "Latency (cycles)", "L1DMiss (%)", "L2Miss (%)", "L3Miss (%)"]
    formatters = {
        "BufferSize": format_bytes,
        "Latency": "{:.2f}".format,
        "L1_miss": lambda x: f"{x * 100:.2f}",
        "L2_miss": lambda x: f"{x * 100:.2f}",
        "L3_miss": lambda x: f"{x * 100:.2f}",
    }

    title = f"Cache Hierarchy Analysis (PaddedElementSize: {format_bytes(CACHE_LINE_SIZE)})"
    table = d[cols].to_string(index=False, header=headers, formatters=formatters)
    print(title)
    print("=" * len(table.split("\n")[0]))
    print(table)
    print()
    print(
        f"Estimated latencies:"
        f"  L1={l1_latency:.1f}  L2={l2_latency:.1f}  L3={l3_latency:.1f} cycles"
    )
    print()


def plot_latency_estimates(
    ax: Axes, l1_latency: float, l2_latency: float, l3_latency: float
) -> None:
    yform = ax.get_yaxis_transform()
    for lat, label, color in [
        (l1_latency, "L1 est.", COLORS["L1_hit"]),
        (l2_latency, "L2 est.", COLORS["L2_hit"]),
        (l3_latency, "L3 est.", COLORS["L3_hit"]),
    ]:
        ax.axhline(lat, color=color, linestyle="--", linewidth=0.9, zorder=0, label=label)
        ax.text(
            1.01,
            lat,
            f"{lat:.1f}",
            ha="left",
            va="center",
            fontsize=7,
            color=color,
            transform=yform,
            clip_on=False,
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("filename")
    parser.add_argument("--l1", required=True)
    parser.add_argument("--l2", required=True)
    parser.add_argument("--l3", required=True)
    parser.add_argument("--output", default="cache_latency.pdf")
    args = parser.parse_args()

    l1 = parse_size(args.l1)
    l2 = parse_size(args.l2)
    l3 = parse_size(args.l3)

    df = load_data(args.filename)
    l1_latency, l2_latency, l3_latency = estimate_cache_latency(df)

    print_cache_table(df, l1_latency, l2_latency, l3_latency)

    data = df[df["PaddedElementSize"] == CACHE_LINE_SIZE].sort_values("BufferSize")
    x = data["BufferSize"]

    fig, (ax_lat, ax_area) = plt.subplots(
        2,
        1,
        figsize=(5.5, 4.6),
        sharex=True,
        gridspec_kw={"height_ratios": [6, 5]},
    )

    ax_lat.plot(
        x, data["Latency"], color=COLORS["latency"], marker="o", linewidth=1.0, label="Latency"
    )
    apply_log2_yaxis(ax_lat, ymin=2, ymax=256)
    ax_lat.set_ylabel("Load latency (cycles)")
    apply_grid(ax_lat)
    plot_cache_boundaries(ax_lat, l1, l2, l3, show_label=True)
    plot_latency_estimates(ax_lat, l1_latency, l2_latency, l3_latency)
    ax_lat.legend(loc="upper left", handlelength=1.5)

    ax_area.stackplot(
        x,
        data["L1_hit"] * 100,
        data["L2_hit"] * 100,
        data["L3_hit"] * 100,
        data["L3_miss"] * 100,
        labels=["L1", "L2", "L3", "DRAM"],
        colors=[COLORS["L1_hit"], COLORS["L2_hit"], COLORS["L3_hit"], COLORS["DRAM"]],
        alpha=0.85,
    )
    ax_area.set_ylabel("Data location (%)")
    ax_area.set_xlabel("Buffer size (bytes)")
    ax_area.set_ylim(0, 100)
    apply_percent_yaxis(ax_area)
    ax_area.legend(loc="upper right", handlelength=1.5, handleheight=0.8)
    apply_grid(ax_area)
    plot_cache_boundaries(ax_area, l1, l2, l3, show_label=False)

    apply_log2_xaxis(ax_area)
    fig.tight_layout()
    fig.savefig(args.output, bbox_inches="tight")
    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
