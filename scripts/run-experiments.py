#!/usr/bin/env python3
"""Run every Small Office Network experiment and collect the results.

Usage:
    python3 scripts/run-experiments.py [--ns3-dir PATH] [--runs N] [--sim-time S]

* Builds the simulation, then runs each experiment through ``./ns3 run``.
* Each run writes to results/runs/<run_id>/ (flows.csv, metadata.txt,
  validation.txt, routing-tables.txt, flowmon.xml, console.txt).
* All per-flow rows are merged into results/all-results.csv.
* results/run-manifest.json records the ns-3 version, project git commit,
  every run's exact command line and its exit status.
* Failed runs are listed at the end and the script exits non-zero.
"""
import argparse
import csv
import datetime
import json
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
RESULTS = PROJECT / "results"

# LAN profiles used by the load experiment (see docs/experiment-design.md).
LAN_PROFILES = {
    "lan100": {"csmaRate": "100Mbps", "csmaDelay": "500ns",
               "label": "100 Mbps LAN, 500 ns delay (realistic office)"},
    "lan10": {"csmaRate": "10Mbps", "csmaDelay": "500ns",
              "label": "10 Mbps LAN, 500 ns delay (bottleneck)"},
    "lan100_2ms": {"csmaRate": "100Mbps", "csmaDelay": "2ms",
                   "label": "100 Mbps LAN, 2 ms delay (proposal value)"},
}
LOADS = {"low": 1.0, "medium": 5.0, "high": 10.0}


def experiments(runs):
    """Yields (run_id, experiment_name, lan_profile, run, args)."""
    base = LAN_PROFILES["lan100"]
    lan = {"csmaRate": base["csmaRate"], "csmaDelay": base["csmaDelay"]}
    yield ("e1_basic_connectivity", "Experiment 1: basic connectivity", "lan100", 1,
           {**lan, "flows": "intra", "loadMbps": 1, "enableFilter": "false"})
    yield ("e2_interdepartmental_routing", "Experiment 2: interdepartmental routing", "lan100", 1,
           {**lan, "flows": "inter", "loadMbps": 1, "enableFilter": "false"})
    yield ("e3_access_control_off", "Experiment 3: access control (filter off)", "lan100", 1,
           {**lan, "flows": "all", "loadMbps": 1, "enableFilter": "false"})
    yield ("e3_access_control_on", "Experiment 3: access control (filter on)", "lan100", 1,
           {**lan, "flows": "all", "loadMbps": 1, "enableFilter": "true",
            "filterSendIcmp": "true"})
    for profile, p in LAN_PROFILES.items():
        for level, mbps in LOADS.items():
            for run in range(1, runs + 1):
                yield (f"e4_load_{level}_{profile}_run{run}",
                       f"Experiment 4: {level} load", profile, run,
                       {"csmaRate": p["csmaRate"], "csmaDelay": p["csmaDelay"],
                        "flows": "all", "loadMbps": mbps, "enableFilter": "false"})


def find_ns3(cli_value):
    if cli_value:
        return Path(cli_value).resolve()
    marker = PROJECT / ".ns3-path"
    if marker.exists():
        return Path(marker.read_text().strip())
    sys.exit("error: ns-3 location unknown. Run scripts/link-into-ns3.sh /path/to/ns-3.49 "
             "first, or pass --ns3-dir.")


def git_commit():
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROJECT, capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ns3-dir", help="path to the ns-3.49 directory")
    ap.add_argument("--runs", type=int, default=3,
                    help="independent runs (RNG run numbers) per load point (default 3)")
    ap.add_argument("--sim-time", type=float, default=60.0, help="simulated seconds per run")
    ap.add_argument("--seed", type=int, default=1, help="RNG seed shared by all runs")
    ap.add_argument("--only", help="run only experiments whose id contains this text")
    args = ap.parse_args()

    ns3 = find_ns3(args.ns3_dir)
    if not (ns3 / "ns3").exists():
        sys.exit(f"error: {ns3} is not an ns-3 directory")
    version = (ns3 / "VERSION").read_text().strip() if (ns3 / "VERSION").exists() else "unknown"

    print(f"ns-3 {version} at {ns3}\nbuilding office-network ...", flush=True)
    build = subprocess.run(["./ns3", "build", "office-network"], cwd=ns3, capture_output=True,
                           text=True)
    if build.returncode != 0:
        print(build.stdout[-3000:], build.stderr[-3000:])
        sys.exit("error: build failed")

    runs_dir = RESULTS / "runs"
    if runs_dir.exists() and not args.only:
        shutil.rmtree(runs_dir)
    runs_dir.mkdir(parents=True, exist_ok=True)

    manifest = {"ns3_version": version, "project_commit": git_commit(),
                "started": datetime.datetime.now().isoformat(timespec="seconds"),
                "sim_time_s": args.sim_time, "seed": args.seed, "runs": []}
    rows, failed = [], []
    plan = [e for e in experiments(args.runs) if not args.only or args.only in e[0]]
    for i, (run_id, name, profile, run, params) in enumerate(plan, 1):
        out = runs_dir / run_id
        params = {**params, "experiment": run_id, "simTime": args.sim_time,
                  "seed": args.seed, "run": run, "outDir": str(out)}
        prog = "office-network " + " ".join(f"--{k}={v}" for k, v in params.items())
        proc = subprocess.run(["./ns3", "run", "--no-build", prog], cwd=ns3,
                              capture_output=True, text=True)
        out.mkdir(parents=True, exist_ok=True)
        (out / "console.txt").write_text(proc.stdout + proc.stderr)
        ok = proc.returncode == 0 and (out / "flows.csv").exists()
        print(f"[{i:2d}/{len(plan)}] {'OK  ' if ok else 'FAIL'} {run_id}", flush=True)
        manifest["runs"].append({"run_id": run_id, "experiment_name": name,
                                 "lan_profile": profile, "command": prog,
                                 "exit_code": proc.returncode, "ok": ok})
        if not ok:
            failed.append(run_id)
            continue
        with open(out / "flows.csv", newline="") as fh:
            for r in csv.DictReader(fh):
                rows.append({"run_id": run_id, "experiment_name": name,
                             "lan_profile": profile,
                             "lan_label": LAN_PROFILES[profile]["label"], **r})

    if rows:
        with open(RESULTS / "all-results.csv", "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
    manifest["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    manifest["failed_runs"] = failed
    (RESULTS / "run-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"\n{len(plan) - len(failed)}/{len(plan)} runs succeeded; "
          f"{len(rows)} flow rows -> results/all-results.csv")
    if failed:
        print("FAILED RUNS:\n  " + "\n  ".join(failed))
        print("See results/runs/<run_id>/console.txt for each failure.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
