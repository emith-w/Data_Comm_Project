#!/usr/bin/env python3
"""Validate results/all-results.csv, draw the figures and write results/summary.md.

Usage:
    python3 scripts/analyze-results.py [--csv results/all-results.csv]

Nothing here invents data: if an experiment's rows are missing, its figure
is skipped and the gap is reported.
"""
import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

PROJECT = Path(__file__).resolve().parent.parent
FIGURES = PROJECT / "figures"

# Reference categorical palette, slots 1-3 in fixed order (one per LAN profile).
COLORS = {"lan100": "#2a78d6", "lan10": "#eb6834", "lan100_2ms": "#1baf7a"}
MARKERS = {"lan100": "o", "lan10": "s", "lan100_2ms": "^"}
PROFILE_ORDER = ["lan100", "lan10", "lan100_2ms"]
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"
LOAD_ORDER = {"low": 1, "medium": 2, "high": 3}

plt.rcParams.update({
    "font.size": 10, "axes.edgecolor": INK2, "axes.labelcolor": INK, "axes.titlesize": 11,
    "axes.titleweight": "bold", "xtick.color": INK2, "ytick.color": INK2,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False,
    "axes.axisbelow": True, "figure.dpi": 150, "savefig.bbox": "tight",
})

INT_COLS = ["tx_packets", "rx_packets", "lost_packets", "blocked_packets", "queue_drop_packets",
            "channel_drop_packets", "arp_drop_packets", "router_rx_packets",
            "router_forwarded_packets", "app_tx_packets", "app_rx_packets"]


# --------------------------------------------------------------------------- validation
def validate(df):
    """Returns a list of (ok, message) results-validation checks."""
    checks = []

    def check(ok, msg):
        checks.append((bool(ok), msg))

    check(len(df) > 0, f"results file has rows ({len(df)})")
    num = ["throughput_mbps", "packet_delivery_ratio", "packet_loss_ratio"] + INT_COLS
    check((df[num].fillna(0) >= 0).all().all(), "all counters and metrics are non-negative")
    check((df.rx_packets <= df.tx_packets).all(), "rx_packets <= tx_packets for every flow")
    check(df.packet_delivery_ratio.between(0, 100).all(), "PDR within 0-100 %")
    check(((df.packet_delivery_ratio + df.packet_loss_ratio - 100).abs() < 1e-3).all(),
          "PDR + loss = 100 % for every flow")
    explained = (df.blocked_packets + df.queue_drop_packets + df.channel_drop_packets
                 + df.arp_drop_packets)
    check((df.tx_packets - df.rx_packets == explained).all(),
          "every missing packet has a recorded cause (blocked/queue/channel/ARP)")
    check(((df.app_tx_packets == df.tx_packets) & (df.app_rx_packets == df.rx_packets)).all(),
          "application counters match FlowMonitor counters")
    tput = df.rx_packets * (df.packet_size + 28) * 8 / (df.duration_s * 1e6)
    check(((tput - df.throughput_mbps).abs() < 1e-4).all(),
          "throughput = rx_packets x (payload + 28 B IP/UDP) x 8 / duration")
    check(df.groupby("run_id").seed.nunique().eq(1).all() and df.run.notna().all(),
          "every run records its seed and run number")
    on = df[df.filter_enabled]
    denied = on[on.destination == "AdminPC1"]
    check(len(denied) > 0 and (denied.rx_packets == 0).all()
          and (denied.router_forwarded_packets == 0).all(),
          "filter on: Staff -> AdminPC1 received 0 and router forwarded 0")
    check((on[on.destination != "AdminPC1"].blocked_packets == 0).all(),
          "filter on: no other flow had packets blocked")
    check((df[~df.filter_enabled].blocked_packets == 0).all(),
          "filter off: no packets blocked anywhere")
    return checks


# --------------------------------------------------------------------------- aggregation
def load_points(df):
    """Network-wide metrics per (profile, offered load, run)."""
    e4 = df[df.run_id.str.startswith("e4_")].copy()
    if e4.empty:
        return e4
    e4["delay_x_rx"] = e4.average_delay_ms.fillna(0) * e4.rx_packets
    g = e4.groupby(["lan_profile", "offered_load_mbps", "run"]).agg(
        tx=("tx_packets", "sum"), rx=("rx_packets", "sum"),
        tput=("throughput_mbps", "sum"), delay_x_rx=("delay_x_rx", "sum"),
        queue=("queue_drop_packets", "sum"), channel=("channel_drop_packets", "sum"),
        arp=("arp_drop_packets", "sum"), flows=("flow_id", "count")).reset_index()
    g["pdr"] = 100 * g.rx / g.tx
    g["loss"] = 100 - g.pdr
    g["delay"] = g.delay_x_rx / g.rx
    g["offered_total"] = g.offered_load_mbps * g.flows
    return g


def mean_range(points, col):
    s = points.groupby(["lan_profile", "offered_load_mbps"])[col]
    out = s.agg(["mean", "min", "max"]).reset_index()
    return out


# --------------------------------------------------------------------------- figures
def yerr(s):
    """Asymmetric min-max error bars (clipped: identical runs give tiny float noise)."""
    return [(s["mean"] - s["min"]).clip(lower=0), (s["max"] - s["mean"]).clip(lower=0)]


def line_vs_load(points, col, ylabel, title, fname, labels, log=False, ymax=None):
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    stats = mean_range(points, col)
    for p in PROFILE_ORDER:
        s = stats[stats.lan_profile == p].sort_values("offered_load_mbps")
        if s.empty:
            continue
        err = yerr(s)
        ax.errorbar(s.offered_load_mbps, s["mean"], yerr=err, color=COLORS[p], marker=MARKERS[p],
                    markersize=7, linewidth=2, capsize=3, label=labels[p],
                    markeredgecolor="white", markeredgewidth=1)
    ax.set_xticks(sorted(points.offered_load_mbps.unique()))
    ax.set_xlabel("Offered load per flow (Mbps of UDP payload, 6 flows)")
    ax.set_ylabel(ylabel)
    ax.set_title(title, loc="left")
    if log:
        ax.set_yscale("log")
    if ymax is not None:
        ax.set_ylim(0, ymax)
    ax.legend(loc="best", fontsize=8.5)
    fig.text(0.0, -0.04, "Mean of runs; bars show min-max across RNG runs. Source: "
             "results/all-results.csv", fontsize=7.5, color=INK2)
    fig.savefig(FIGURES / fname)
    plt.close(fig)
    return fname


def throughput_figure(points, labels):
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    stats = mean_range(points, "tput")
    loads = sorted(points.offered_load_mbps.unique())
    ideal = [l * 6 * (1024 + 28) / 1024 for l in loads]
    ax.plot(loads, ideal, color=INK2, linestyle="--", linewidth=1.2, zorder=5,
            label="Offered (6 flows, incl. IP/UDP headers)")
    for p in PROFILE_ORDER:
        s = stats[stats.lan_profile == p].sort_values("offered_load_mbps")
        if s.empty:
            continue
        ax.errorbar(s.offered_load_mbps, s["mean"], yerr=yerr(s),
                    color=COLORS[p], marker=MARKERS[p], markersize=7, linewidth=2, capsize=3,
                    label=labels[p], markeredgecolor="white", markeredgewidth=1)
    ax.set_xticks(loads)
    ax.set_xlabel("Offered load per flow (Mbps of UDP payload, 6 flows)")
    ax.set_ylabel("Total delivered throughput (Mbps)")
    ax.set_title("Throughput vs offered load", loc="left")
    ax.set_ylim(0, None)
    ax.legend(loc="upper left", fontsize=8.5)
    fig.text(0.0, -0.08, "Mean of runs; bars show min-max across RNG runs. The 100 Mbps line "
             "lies on the offered line (no loss).\nSource: results/all-results.csv",
             fontsize=7.5, color=INK2)
    fig.savefig(FIGURES / "fig1-throughput-vs-load.png")
    plt.close(fig)
    return "fig1-throughput-vs-load.png"


def access_control_figure(df):
    off = df[df.run_id == "e3_access_control_off"].set_index("flow_id")
    on = df[df.run_id == "e3_access_control_on"].set_index("flow_id")
    if off.empty or on.empty:
        return None
    ids = sorted(off.index)
    names = [f"{off.loc[i, 'source']}\n→ {off.loc[i, 'destination']}" for i in ids]
    fig, ax = plt.subplots(figsize=(7.6, 4.0))
    w = 0.38
    x = range(len(ids))
    b1 = ax.bar([i - w / 2 - 0.01 for i in x], [off.loc[i, "packet_delivery_ratio"] for i in ids],
                w, color=COLORS["lan100"], label="Filter disabled (before)")
    b2 = ax.bar([i + w / 2 + 0.01 for i in x], [on.loc[i, "packet_delivery_ratio"] for i in ids],
                w, color=COLORS["lan10"], label="Filter enabled (after)")
    for i, fid in enumerate(ids):
        if on.loc[fid, "blocked_packets"] > 0:
            ax.annotate(f"blocked\n{int(on.loc[fid, 'blocked_packets'])} pkts",
                        (i + w / 2, 2), ha="center", va="bottom", fontsize=7.5, color=INK)
    ax.set_xticks(list(x))
    ax.set_xticklabels(names, fontsize=8)
    ax.set_ylabel("Packet delivery ratio (%)")
    ax.set_ylim(0, 115)
    ax.set_title("Access control: delivery per flow before vs after filtering", loc="left")
    ax.legend(loc="upper center", ncol=2, fontsize=8.5, bbox_to_anchor=(0.5, 1.0))
    fig.text(0.0, -0.06, "100 Mbps LAN, 1 Mbps per flow, 60 s. Source: experiment e3 in "
             "results/all-results.csv", fontsize=7.5, color=INK2)
    fig.savefig(FIGURES / "fig5-access-control.png")
    plt.close(fig)
    return "fig5-access-control.png"


def loss_cause_figure(df):
    e4 = df[df.run_id.str.startswith("e4_") & (df.run == 1)]
    if e4.empty:
        return None
    g = e4.groupby(["lan_profile", "offered_load_mbps"])[
        ["tx_packets", "queue_drop_packets", "channel_drop_packets", "arp_drop_packets"]].sum()
    fig, ax = plt.subplots(figsize=(7.6, 4.0))
    labels, q, a = [], [], []
    for p in PROFILE_ORDER:
        for load in sorted(e4.offered_load_mbps.unique()):
            if (p, load) not in g.index:
                continue
            r = g.loc[(p, load)]
            labels.append(f"{p}\n{load:g} Mbps")
            q.append(100 * r.queue_drop_packets / r.tx_packets)
            a.append(100 * (r.arp_drop_packets + r.channel_drop_packets) / r.tx_packets)
    x = range(len(labels))
    ax.bar(x, q, 0.7, color=COLORS["lan100"], label="Transmit queue full", edgecolor="white",
           linewidth=1)
    ax.bar(x, a, 0.7, bottom=q, color=COLORS["lan10"], label="ARP / channel", edgecolor="white",
           linewidth=1)
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, fontsize=7.5)
    ax.set_ylabel("Share of sent packets dropped (%)")
    ax.set_title("Why packets were dropped (filter off, run 1)", loc="left")
    for i, (qv, av) in enumerate(zip(q, a)):
        if qv + av > 0:
            ax.annotate(f"{qv + av:.1f}%", (i, qv + av), ha="center", va="bottom",
                        fontsize=7.5, color=INK, xytext=(0, 2), textcoords="offset points")
    ax.set_ylim(0, 105)
    ax.legend(fontsize=8.5)
    small = max(a) if a else 0.0
    fig.text(0.0, -0.1, "lan100 = 100 Mbps/500 ns, lan10 = 10 Mbps/500 ns, lan100_2ms = "
             f"100 Mbps/2 ms. ARP/channel drops here are at most {small:.2f}% of sent packets "
             "(too small to see).\nSource: results/all-results.csv (run 1 of each load point)",
             fontsize=7.5, color=INK2)
    fig.savefig(FIGURES / "fig6-drop-causes.png")
    plt.close(fig)
    return "fig6-drop-causes.png"


# --------------------------------------------------------------------------- summary
def md_table(frame):
    cols = list(frame.columns)
    lines = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for _, r in frame.iterrows():
        lines.append("| " + " | ".join(str(r[c]) for c in cols) + " |")
    return "\n".join(lines)


def flow_table(df, run_id):
    d = df[df.run_id == run_id]
    if d.empty:
        return f"_No rows for `{run_id}`._"
    t = pd.DataFrame({
        "Flow": d.flow_id, "Source → Destination": d.source + " → " + d.destination,
        "Tx": d.tx_packets, "Rx": d.rx_packets, "Blocked": d.blocked_packets,
        "PDR %": d.packet_delivery_ratio.round(2),
        "Throughput Mbps": d.throughput_mbps.round(4),
        "Avg delay ms": d.average_delay_ms.round(4).fillna("–")})
    return md_table(t)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--csv", default=str(PROJECT / "results" / "all-results.csv"))
    args = ap.parse_args()
    path = Path(args.csv)
    if not path.exists():
        print(f"error: {path} not found. Run scripts/run-experiments.py first; "
              "no figures were generated.")
        return 1

    df = pd.read_csv(path)
    df["filter_enabled"] = df.filter_enabled.astype(str).str.lower() == "true"
    checks = validate(df)
    for ok, msg in checks:
        print(("PASS  " if ok else "FAIL  ") + msg)

    FIGURES.mkdir(exist_ok=True)
    made = []
    points = load_points(df)
    labels = (df.drop_duplicates("lan_profile").set_index("lan_profile").lan_label.to_dict())
    if points.empty:
        print("note: no Experiment 4 rows - load figures skipped")
    else:
        made.append(throughput_figure(points, labels))
        made.append(line_vs_load(points, "pdr", "Packet delivery ratio (%)",
                                 "Packet delivery ratio vs offered load",
                                 "fig2-pdr-vs-load.png", labels, ymax=105))
        made.append(line_vs_load(points, "delay", "Average end-to-end delay (ms, log scale)",
                                 "Average end-to-end delay vs offered load",
                                 "fig3-delay-vs-load.png", labels, log=True))
        made.append(line_vs_load(points, "loss", "Packet loss (%)",
                                 "Packet loss vs offered load", "fig4-loss-vs-load.png",
                                 labels, ymax=105))
    for f in (access_control_figure(df), loss_cause_figure(df)):
        if f:
            made.append(f)
        else:
            print("note: rows missing for a figure - skipped")

    # ---- summary.md (every number below is read from the CSV)
    out = ["# Results Summary", "",
           f"Generated by `scripts/analyze-results.py` from `{path.relative_to(PROJECT)}` "
           f"({len(df)} flow rows, {df.run_id.nunique()} runs). Every number here is from an "
           "actual ns-3.49 run.", "", "## Results validation", ""]
    out += [f"- {'✅' if ok else '❌'} {msg}" for ok, msg in checks]
    out += ["", "## Experiment 1 — basic connectivity (same department)", "",
            flow_table(df, "e1_basic_connectivity"), "",
            "## Experiment 2 — interdepartmental routing (filter off)", "",
            flow_table(df, "e2_interdepartmental_routing"), "",
            "## Experiment 3 — access control, filter OFF", "",
            flow_table(df, "e3_access_control_off"), "",
            "## Experiment 3 — access control, filter ON", "",
            flow_table(df, "e3_access_control_on"), ""]
    if not points.empty:
        s = points.groupby(["lan_profile", "offered_load_mbps"]).agg(
            runs=("run", "count"), pdr=("pdr", "mean"), pdr_min=("pdr", "min"),
            pdr_max=("pdr", "max"), tput=("tput", "mean"), delay=("delay", "mean"),
            loss=("loss", "mean")).reset_index()
        s["lan_profile"] = pd.Categorical(s.lan_profile, PROFILE_ORDER)
        s = s.sort_values(["lan_profile", "offered_load_mbps"])
        t = pd.DataFrame({
            "LAN profile": s.lan_profile.astype(str), "Load/flow Mbps": s.offered_load_mbps,
            "Runs": s.runs, "Total throughput Mbps": s.tput.round(3),
            "PDR % (mean)": s.pdr.round(2),
            "PDR % (min–max)": s.pdr_min.round(2).astype(str) + "–" + s.pdr_max.round(2).astype(str),
            "Loss %": s.loss.round(2), "Avg delay ms": s.delay.round(3)})
        out += ["## Experiment 4 — traffic load (all 6 flows, filter off)", "",
                "Network-wide values: throughput is summed over the 6 flows; PDR/loss use "
                "total rx / total tx; delay is the packet-weighted mean.", "", md_table(t), ""]
        out += ["LAN profiles: " + "; ".join(f"`{k}` = {v}" for k, v in labels.items()), ""]
    out += ["## Figures", ""] + [f"- `figures/{f}`" for f in made] + [""]
    (PROJECT / "results" / "summary.md").write_text("\n".join(out))
    print(f"\nfigures: {', '.join(made)}\nsummary: results/summary.md")
    return 0 if all(ok for ok, _ in checks) else 1


if __name__ == "__main__":
    sys.exit(main())
