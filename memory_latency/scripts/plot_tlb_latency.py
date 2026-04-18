import argparse

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib import ticker
from matplotlib.axes import Axes

from plot_utils import (
    CACHE_LINE_SIZE,
    PAGE_SIZE,
    apply_grid,
    apply_log2_xaxis,
    apply_log2_yaxis,
    apply_percent_yaxis,
    setup_rcparams,
)

setup_rcparams()

COLORS = {
    "huge": "#0072B2",
    "reg": "#D55E00",
    "tlb_rate": "#CC79A7",
    "tlb_cost": "#009E73",
}


def load_data(filename: str) -> pd.DataFrame:
    df = pd.read_csv(filename)
    num_loads = df["NumLogicalLoads"]
    df["Latency"] = df["Cycles"] / num_loads
    df["L1TLBMissRate"] = df["L1TLBMisses"] / num_loads * 100
    df["L2TLBMissRate"] = df["L2TLBMisses"] / num_loads * 100
    return df


def plot_tlb_boundary(ax: Axes, size: int, label: str, color: str, show_label: bool = True) -> None:
    xform = ax.get_xaxis_transform()
    ax.axvline(size, color=color, linestyle="--", linewidth=0.7, zorder=0)
    if show_label:
        ax.text(
            size,
            1.01,
            label,
            ha="center",
            va="bottom",
            fontsize=7,
            color=color,
            transform=xform,
            clip_on=False,
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("filename")
    parser.add_argument("--l1-tlb-entries", required=True, type=int)
    parser.add_argument("--l2-tlb-entries", required=True, type=int)
    parser.add_argument("--output", default="tlb_latency.pdf")
    args = parser.parse_args()

    df = load_data(args.filename)
    tlb_elem = df[df["PaddedElementSize"] != CACHE_LINE_SIZE]["PaddedElementSize"].iloc[0]
    mask_huge = (df["PaddedElementSize"] == tlb_elem) & (df["PageSize"] != PAGE_SIZE)
    mask_reg = (df["PaddedElementSize"] == tlb_elem) & (df["PageSize"] == PAGE_SIZE)
    hugepage = df[mask_huge].sort_values("BufferSize")
    regular = df[mask_reg].sort_values("BufferSize")

    l1_tlb = args.l1_tlb_entries * PAGE_SIZE
    l2_tlb = args.l2_tlb_entries * PAGE_SIZE

    diff = (
        hugepage[["BufferSize", "Latency"]]
        .rename(columns={"Latency": "Latency_huge"})
        .merge(
            regular[["BufferSize", "Latency"]],
            on="BufferSize",
        )
    )
    diff["Overhead"] = diff["Latency"] - diff["Latency_huge"]

    fig, (ax_lat, ax_diff, ax_tlb) = plt.subplots(
        3,
        1,
        figsize=(5.5, 6.4),
        sharex=True,
    )

    ax_lat.plot(
        hugepage["BufferSize"],
        hugepage["Latency"],
        label="Hugepage (2 MiB)",
        color=COLORS["huge"],
        marker="o",
        linewidth=1.0,
    )
    ax_lat.plot(
        regular["BufferSize"],
        regular["Latency"],
        label="Regular page (4 KiB)",
        color=COLORS["reg"],
        marker="s",
        linewidth=1.0,
    )
    apply_log2_yaxis(ax_lat)
    ax_lat.set_ylabel("Load latency (cycles)")
    ax_lat.legend(loc="upper left", handlelength=1.5)
    apply_grid(ax_lat)
    plot_tlb_boundary(ax_lat, l1_tlb, "L1 TLB", "gray", show_label=True)
    plot_tlb_boundary(ax_lat, l2_tlb, "L2 TLB", "gray", show_label=True)

    x = diff["BufferSize"]
    ovhd = diff["Overhead"]
    ax_diff.axhline(0, color="black", linewidth=0.7)
    ax_diff.fill_between(x, ovhd, 0, where=ovhd >= 0, color=COLORS["reg"], alpha=0.25, linewidth=0)
    ax_diff.fill_between(x, ovhd, 0, where=ovhd < 0, color="gray", alpha=0.20, linewidth=0)
    ax_diff.plot(x, ovhd, color=COLORS["reg"], marker="s", linewidth=1.0)
    ax_diff.yaxis.set_major_locator(ticker.MultipleLocator(10))
    ax_diff.set_ylabel("TLB miss penalty (cycles)")
    apply_grid(ax_diff)
    plot_tlb_boundary(ax_diff, l1_tlb, "L1 TLB", "gray", show_label=False)
    plot_tlb_boundary(ax_diff, l2_tlb, "L2 TLB", "gray", show_label=False)

    ax_tlb.plot(
        regular["BufferSize"],
        regular["L1TLBMissRate"],
        label="L1 TLB miss rate",
        color=COLORS["tlb_rate"],
        marker="^",
        linewidth=1.0,
    )
    ax_tlb.plot(
        regular["BufferSize"],
        regular["L2TLBMissRate"],
        label="L2 TLB miss rate",
        color=COLORS["tlb_cost"],
        marker="v",
        linewidth=1.0,
    )
    ax_tlb.legend(loc="upper left", handlelength=1.5)
    ax_tlb.set_ylabel("TLB miss rate (%)")
    ax_tlb.set_xlabel("Buffer size (bytes)")
    apply_percent_yaxis(ax_tlb)
    apply_grid(ax_tlb)
    plot_tlb_boundary(ax_tlb, l1_tlb, "L1 TLB", "gray", show_label=False)
    plot_tlb_boundary(ax_tlb, l2_tlb, "L2 TLB", "gray", show_label=False)

    apply_log2_xaxis(ax_tlb)
    fig.tight_layout()
    fig.savefig(args.output, bbox_inches="tight")
    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
