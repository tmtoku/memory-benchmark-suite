import argparse

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib import ticker
from matplotlib.axes import Axes

from plot_utils import (
    apply_grid,
    apply_log2_xaxis,
    apply_percent_yaxis,
    setup_rcparams,
)

setup_rcparams()

MISS_RATE_THRESHOLD = 1e-4

COLORS = {
    "additional_latency": "#000000",
    "L1_TLB_miss": "#CC79A7",
    "L2_TLB_miss": "#009E73",
}


def load_data(filename: str) -> pd.DataFrame:
    df = pd.read_csv(filename)
    num_loads = df["NumLogicalLoads"]
    df["Latency"] = df["Cycles"] / num_loads
    df["NumVirtualPages"] = df["BufferSize"] // df["PageSize"]
    df["L1_TLB_miss"] = (df["L1TLBMisses"] / num_loads).clip(0, 1)
    df["L2_TLB_miss"] = (df["L2TLBMisses"] / num_loads).clip(0, 1)
    df["L1_TLB_hit"] = (1 - df["L1_TLB_miss"]).clip(0, 1)
    df["L2_TLB_hit"] = (df["L1_TLB_miss"] - df["L2_TLB_miss"]).clip(0, 1)
    return df


def estimate_tlb_latency(df: pd.DataFrame) -> tuple[float, float, float]:
    d = df.sort_values("NumVirtualPages")

    # L1 TLB-dominant region: L1 TLB miss rate is negligible.
    # observed latency ~ L1_TLB_load_latency
    l1_tlb_region = d["L1_TLB_miss"] < MISS_RATE_THRESHOLD
    l1_tlb_load_latency = d[l1_tlb_region]["Latency"].median()

    # L2 TLB-dominant region: L1 TLB mostly misses, L2 TLB mostly hits.
    # observed latency
    #   ~ L1_TLB_hit * L1_TLB_load_latency
    #     + L2_TLB_hit * L2_TLB_load_latency
    l2_tlb_region = (d["L1_TLB_miss"] >= MISS_RATE_THRESHOLD) & (
        d["L2_TLB_miss"] < MISS_RATE_THRESHOLD
    )
    l2_tlb_load_latency = (
        (d[l2_tlb_region]["Latency"] - d[l2_tlb_region]["L1_TLB_hit"] * l1_tlb_load_latency)
        / d[l2_tlb_region]["L2_TLB_hit"]
    ).median()

    # Page table-dominant region: L2 TLB miss rate is non-negligible.
    # observed latency
    #   ~ L1_TLB_hit * L1_TLB_load_latency
    #     + L2_TLB_hit * L2_TLB_load_latency
    #     + L2_TLB_miss * page_table_load_latency
    page_table_region = d["L2_TLB_miss"] >= MISS_RATE_THRESHOLD
    page_table_load_latency = (
        (
            d[page_table_region]["Latency"]
            - d[page_table_region]["L1_TLB_hit"] * l1_tlb_load_latency
            - d[page_table_region]["L2_TLB_hit"] * l2_tlb_load_latency
        )
        / d[page_table_region]["L2_TLB_miss"]
    ).median()

    return l1_tlb_load_latency, l2_tlb_load_latency, page_table_load_latency


def print_tlb_latency_estimates(
    l1_tlb_load_latency: float,
    l1_tlb_miss_cost: float,
    l2_tlb_miss_cost: float,
) -> None:
    print(
        "Estimated latency components:"
        f"  L1 TLB-hit load (baseline)={l1_tlb_load_latency:.1f}"
        f"  L1 TLB miss=+{l1_tlb_miss_cost:.1f}"
        f"  L2 TLB miss=+{l2_tlb_miss_cost:.1f} cycles"
    )
    print()


def plot_tlb_boundaries(
    ax: Axes,
    num_l1_tlb_entries: int,
    num_l2_tlb_entries: int,
    show_label: bool = True,
) -> None:
    xform = ax.get_xaxis_transform()
    for num_entries, label in [
        (num_l1_tlb_entries, "L1 TLB"),
        (num_l2_tlb_entries, "L2 TLB"),
    ]:
        ax.axvline(num_entries, color="gray", linestyle=":", linewidth=0.8, zorder=0)
        if show_label:
            ax.text(
                num_entries,
                1.01,
                label,
                ha="center",
                va="bottom",
                fontsize=7,
                color="gray",
                transform=xform,
                clip_on=False,
            )


def plot_tlb_miss_cost_estimates(
    ax: Axes, l1_tlb_miss_cost: float, l2_tlb_miss_cost: float
) -> None:
    yform = ax.get_yaxis_transform()
    for cost, label, color in [
        (l1_tlb_miss_cost, "L1 TLB miss est.", COLORS["L1_TLB_miss"]),
        (
            l1_tlb_miss_cost + l2_tlb_miss_cost,
            "L1 + L2 TLB miss est.",
            COLORS["L2_TLB_miss"],
        ),
    ]:
        ax.axhline(cost, color=color, linestyle="--", linewidth=0.9, zorder=0, label=label)
        ax.text(
            1.01,
            cost,
            f"{cost:.1f}",
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
    parser.add_argument("--l1-tlb-entries", required=True, type=int)
    parser.add_argument("--l2-tlb-entries", required=True, type=int)
    parser.add_argument("--output", default="tlb_latency.pdf")
    args = parser.parse_args()

    num_l1_tlb_entries = args.l1_tlb_entries
    num_l2_tlb_entries = args.l2_tlb_entries

    df = load_data(args.filename)
    l1_tlb_load_latency, l2_tlb_load_latency, page_table_load_latency = estimate_tlb_latency(df)
    l1_tlb_miss_cost = l2_tlb_load_latency - l1_tlb_load_latency
    l2_tlb_miss_cost = page_table_load_latency - l2_tlb_load_latency

    print_tlb_latency_estimates(l1_tlb_load_latency, l1_tlb_miss_cost, l2_tlb_miss_cost)

    data = df.sort_values("NumVirtualPages")
    data["AdditionalLoadLatency"] = data["Latency"] - l1_tlb_load_latency

    fig, (ax_latency, ax_tlb) = plt.subplots(
        2,
        1,
        figsize=(5.5, 4.8),
        sharex=True,
    )

    ax_latency.plot(
        data["NumVirtualPages"],
        data["AdditionalLoadLatency"],
        label="Additional latency",
        color=COLORS["additional_latency"],
        marker="o",
        linewidth=1.0,
    )
    ax_latency.yaxis.set_major_locator(ticker.MultipleLocator(5))
    ax_latency.set_ylabel("Additional load latency (cycles)")
    apply_grid(ax_latency)
    plot_tlb_boundaries(ax_latency, num_l1_tlb_entries, num_l2_tlb_entries, show_label=True)
    plot_tlb_miss_cost_estimates(ax_latency, l1_tlb_miss_cost, l2_tlb_miss_cost)
    ax_latency.legend(loc="upper left", handlelength=1.5)

    ax_tlb.plot(
        data["NumVirtualPages"],
        data["L1_TLB_miss"] * 100,
        label="L1 TLB",
        color=COLORS["L1_TLB_miss"],
        marker="^",
        linewidth=1.0,
    )
    ax_tlb.plot(
        data["NumVirtualPages"],
        data["L2_TLB_miss"] * 100,
        label="L2 TLB",
        color=COLORS["L2_TLB_miss"],
        marker="v",
        linewidth=1.0,
    )
    ax_tlb.set_ylabel("TLB miss rate (%)")
    ax_tlb.set_xlabel("Number of virtual pages")
    ax_tlb.legend(loc="upper left", handlelength=1.5)
    apply_percent_yaxis(ax_tlb)
    apply_grid(ax_tlb)
    plot_tlb_boundaries(ax_tlb, num_l1_tlb_entries, num_l2_tlb_entries, show_label=False)

    apply_log2_xaxis(ax_tlb)
    fig.tight_layout()
    fig.savefig(args.output, bbox_inches="tight")

    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
