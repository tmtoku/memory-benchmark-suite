import matplotlib.pyplot as plt
from matplotlib import ticker
from matplotlib.axes import Axes

CACHE_LINE_SIZE: int = 64
PAGE_SIZE: int = 4096


def setup_rcparams() -> None:
    plt.rcParams.update(
        {
            "font.family": "serif",
            "font.size": 9,
            "axes.labelsize": 9,
            "legend.fontsize": 7,
            "xtick.labelsize": 8,
            "ytick.labelsize": 8,
            "lines.linewidth": 1.2,
            "lines.markersize": 1.8,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )


def parse_size(s: str) -> int:
    s = s.strip()
    for suffix, mult in [("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10), ("B", 1)]:
        if s.endswith(suffix):
            return int(float(s[: -len(suffix)]) * mult)
    return int(s)


def format_percent(x: float, _: int | None = None) -> str:
    return f"{x * 100:.2f}"


def format_bytes(n: float, _: int | None = None) -> str:
    v = int(n)
    for unit, thresh in [("G", 1 << 30), ("M", 1 << 20), ("K", 1 << 10)]:
        if v >= thresh and v % thresh == 0:
            return f"{v // thresh}{unit}"
    return str(v)


def apply_log2_xaxis(ax: Axes) -> None:
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_locator(ticker.LogLocator(base=2, numticks=30))
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(format_bytes))
    ax.xaxis.set_minor_locator(ticker.LogLocator(base=2, subs=[2**0.5], numticks=30))
    ax.xaxis.set_minor_formatter(ticker.NullFormatter())
    ax.tick_params(axis="x", which="major", labelrotation=0, labelsize=7)
    ax.tick_params(axis="x", which="minor", length=3)
    ax.margins(x=0.02)


def apply_log2_yaxis(ax: Axes, ymin: float | None = None, ymax: float | None = None) -> None:
    ax.set_yscale("log", base=2)
    if ymin is not None or ymax is not None:
        ax.set_ylim(ymin, ymax)
    ax.yaxis.set_major_locator(ticker.LogLocator(base=2, numticks=12))
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: str(int(v))))
    ax.yaxis.set_minor_locator(ticker.LogLocator(base=2, subs=[2**0.5], numticks=12))
    ax.yaxis.set_minor_formatter(ticker.NullFormatter())


def apply_percent_yaxis(ax: Axes) -> None:
    ax.yaxis.set_major_locator(ticker.MultipleLocator(20))
    ax.yaxis.set_minor_locator(ticker.MultipleLocator(10))
    ax.tick_params(axis="y", which="minor", length=3)


def apply_grid(ax: Axes) -> None:
    ax.grid(visible=True, which="major", alpha=0.30, linewidth=0.6)
    ax.grid(visible=True, which="minor", alpha=0.12, linewidth=0.4)


def plot_cache_boundaries(ax: Axes, l1: int, l2: int, l3: int, show_label: bool = True) -> None:
    xform = ax.get_xaxis_transform()
    for size, label in [(l1, "L1"), (l2, "L2"), (l3, "L3")]:
        ax.axvline(size, color="gray", linestyle=":", linewidth=0.8, zorder=0)
        if show_label:
            ax.text(
                size,
                1.01,
                label,
                ha="center",
                va="bottom",
                fontsize=7,
                color="gray",
                transform=xform,
                clip_on=False,
            )
