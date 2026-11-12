#!/usr/bin/env python3
"""Automated tests for the Small Office Network project.

Run from the project root (after scripts/link-into-ns3.sh):
    python3 -m unittest discover -s tests -v

Each test runs the real ns-3 simulation (short 10 s runs) and checks the
behaviour required by the project brief from its CSV output and exit code.
"""
import csv
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent


def ns3_dir():
    marker = PROJECT / ".ns3-path"
    if not marker.exists():
        raise unittest.SkipTest("run scripts/link-into-ns3.sh /path/to/ns-3.49 first")
    return Path(marker.read_text().strip())


def run_sim(program, **params):
    """Runs an ns-3 program and returns (exit_code, stdout, flow rows by id)."""
    out = Path(tempfile.mkdtemp(prefix="office-test-"))
    if program == "office-network":
        params.setdefault("simTime", 10)
        params["outDir"] = out
    cmd = program + "".join(f" --{k}={v}" for k, v in params.items())
    proc = subprocess.run(["./ns3", "run", "--no-build", cmd], cwd=ns3_dir(),
                          capture_output=True, text=True)
    rows = {}
    if (out / "flows.csv").exists():
        with open(out / "flows.csv", newline="") as fh:
            rows = {int(r["flow_id"]): r for r in csv.DictReader(fh)}
    return proc.returncode, proc.stdout + proc.stderr, rows


class T0Build(unittest.TestCase):
    def test_build(self):
        proc = subprocess.run(["./ns3", "build", "minimal-two-node", "office-network"],
                              cwd=ns3_dir(), capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stdout[-2000:] + proc.stderr[-2000:])
        self.assertNotIn("warning:", proc.stdout + proc.stderr)


class T1Minimal(unittest.TestCase):
    def test_two_node_delivery(self):
        code, out, _ = run_sim("minimal-two-node")
        self.assertEqual(code, 0, out)
        self.assertIn("sent=10 received=10", out)


class T2Network(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.code, cls.out, cls.rows = run_sim("office-network", enableFilter="false")

    def test_all_structural_checks_pass(self):
        self.assertEqual(self.code, 0, self.out)
        self.assertIn("checks failed: 0", self.out)
        for text in ("exactly 4 end-host nodes", "router has 3 IPv4 interfaces",
                     "separate CSMA channels", "IP forwarding enabled",
                     "StaffPC1 -> 192.168.10.10 via 192.168.20.1"):
            self.assertTrue(any(l.startswith("CHECK PASS:") and text in l
                                for l in self.out.splitlines()), f"missing check: {text}")

    def test_addresses_match_plan(self):
        expected = {"AdminPC1": "192.168.10.10", "AdminPC2": "192.168.10.11",
                    "StaffPC1": "192.168.20.10", "StaffPC2": "192.168.20.11"}
        for r in self.rows.values():
            self.assertEqual(r["source_ip"], expected[r["source"]])
            self.assertEqual(r["destination_ip"], expected[r["destination"]])

    def test_same_department_delivery(self):
        for fid in (1, 2):
            r = self.rows[fid]
            self.assertGreater(int(r["tx_packets"]), 0)
            self.assertEqual(r["rx_packets"], r["tx_packets"])
            self.assertEqual(r["router_rx_packets"], "0")  # never touches the router

    def test_cross_department_routed(self):
        for fid in (3, 4, 5, 6):
            r = self.rows[fid]
            self.assertEqual(r["rx_packets"], r["tx_packets"])
            self.assertEqual(r["router_forwarded_packets"], r["tx_packets"])


class T3AccessControl(unittest.TestCase):
    def check_filtered(self, rows):
        for fid in (3, 5):  # Staff -> AdminPC1: DENY
            r = rows[fid]
            self.assertEqual(r["rx_packets"], "0")
            self.assertEqual(r["router_forwarded_packets"], "0")
            self.assertEqual(r["blocked_packets"], r["router_rx_packets"])
            self.assertGreater(int(r["blocked_packets"]), 0)
        for fid in (1, 2, 4, 6):  # everything else still works
            r = rows[fid]
            self.assertEqual(r["blocked_packets"], "0")
            self.assertEqual(r["rx_packets"], r["tx_packets"])

    def test_filter_silent_drop(self):
        code, out, rows = run_sim("office-network", enableFilter="true")
        self.assertEqual(code, 0, out)
        self.check_filtered(rows)

    def test_filter_with_icmp_and_drop_trace(self):
        code, out, rows = run_sim("office-network", enableFilter="true", filterSendIcmp="true")
        self.assertEqual(code, 0, out)
        self.check_filtered(rows)
        for fid in (3, 5):
            self.assertEqual(rows[fid]["router_drop_trace_packets"], rows[fid]["blocked_packets"])

    def test_disabling_restores_baseline(self):
        _, _, off = run_sim("office-network", enableFilter="false")
        for r in off.values():
            self.assertEqual(r["blocked_packets"], "0")
            self.assertEqual(r["rx_packets"], r["tx_packets"])


class T4Traffic(unittest.TestCase):
    def test_flow_selection(self):
        code, out, rows = run_sim("office-network", flows="2,5")
        self.assertEqual(code, 0, out)
        self.assertEqual(sorted(rows), [2, 5])

    def test_bad_flow_selection_rejected(self):
        code, out, _ = run_sim("office-network", flows="9")
        self.assertEqual(code, 2, out)

    def test_offered_load_is_respected(self):
        _, _, rows = run_sim("office-network", flows="1", loadMbps=5)
        r = rows[1]
        # 7 s at 5 Mbps of 1024-byte payloads: one packet every 1.6384 ms.
        # Each flow starts 0-10 ms late (random jitter), so 4266-4273 packets.
        interval = 1024 * 8 / 5e6
        self.assertGreaterEqual(int(r["tx_packets"]), int((7 - 0.01) / interval))
        self.assertLessEqual(int(r["tx_packets"]), int(7 / interval) + 1)

    def test_congestion_loss_is_explained(self):
        code, out, rows = run_sim("office-network", csmaRate="10Mbps", loadMbps=10,
                                  enableFilter="true")
        self.assertEqual(code, 0, out)
        for r in rows.values():
            missing = int(r["tx_packets"]) - int(r["rx_packets"])
            causes = sum(int(r[k]) for k in ("blocked_packets", "queue_drop_packets",
                                             "channel_drop_packets", "arp_drop_packets"))
            self.assertEqual(missing, causes)
        self.assertGreater(int(rows[1]["queue_drop_packets"]), 0)  # congestion did happen

    def test_same_seed_is_reproducible(self):
        a = run_sim("office-network", csmaRate="10Mbps", loadMbps=5, seed=7, run=2)[2]
        b = run_sim("office-network", csmaRate="10Mbps", loadMbps=5, seed=7, run=2)[2]
        for fid in a:
            self.assertEqual(a[fid]["rx_packets"], b[fid]["rx_packets"])
            self.assertEqual(a[fid]["average_delay_ms"], b[fid]["average_delay_ms"])


class T5Analysis(unittest.TestCase):
    def test_analysis_validates_existing_results(self):
        if not (PROJECT / "results" / "all-results.csv").exists():
            self.skipTest("no results yet - run scripts/run-experiments.py")
        proc = subprocess.run(["python3", "scripts/analyze-results.py"], cwd=PROJECT,
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertNotIn("FAIL", proc.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
