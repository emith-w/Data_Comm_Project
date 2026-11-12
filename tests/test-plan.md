# Test Plan and Recorded Outcomes

Testing happens on three levels. Each one runs the real ns-3.49 simulation;
nothing is mocked.

1. **In-simulation checks.** Every run of `office-network` prints `CHECK PASS/FAIL`
   lines, saves them to `validation.txt`, and exits 1 if any fail.
2. **Automated tests.** `python3 -m unittest discover -s tests -v` (15 tests, 10 s runs).
3. **Results validation.** `python3 scripts/analyze-results.py` runs 12 checks on `results/all-results.csv`.

## 1. In-simulation checks

| Area | Check |
|---|---|
| Build | program compiles with no warnings (deprecated 3.49 APIs replaced) |
| Topology | exactly 4 hosts; router has 3 IPv4 interfaces; Admin and Staff on separate CSMA channels with the router on both |
| Addressing | every host and router address read back equals the plan |
| Routing | IP forwarding on both router interfaces; same-subnet routes are direct; cross-subnet routes use the correct router address |
| Filter | the filter is the highest-priority protocol in the router's list routing |
| Policy (filter on) | Staff→AdminPC1: blocked = router rx > 0, router forwarded = 0, destination rx = 0, Drop trace = blocked (ICMP mode) |
| Policy (other flows) | blocked = 0; cross-department flows: router forwarded = router rx |
| Consistency | UdpClient tx = FlowMonitor tx; UdpServer rx = FlowMonitor rx |
| Loss accounting | tx − rx = blocked + queue + channel + ARP drops, exactly |

## 2. Automated tests (`tests/test_project.py`)

| Test | Requirement covered |
|---|---|
| `T0Build.test_build` | simulation builds, no compiler warnings |
| `T1Minimal.test_two_node_delivery` | minimal 2-node sim delivers 10/10 |
| `T2Network.test_all_structural_checks_pass` | 4 hosts, router, separate segments, forwarding, routes |
| `T2Network.test_addresses_match_plan` | IPv4 addresses match the design |
| `T2Network.test_same_department_delivery` | same-department traffic works and never touches the router |
| `T2Network.test_cross_department_routed` | inter-department traffic is forwarded by the router |
| `T3AccessControl.test_filter_silent_drop` | Staff→AdminPC1 blocked; Staff→AdminPC2 and same-dept. allowed |
| `T3AccessControl.test_filter_with_icmp_and_drop_trace` | same, plus the router Drop trace confirms the drop point |
| `T3AccessControl.test_disabling_restores_baseline` | filter off = baseline behaviour |
| `T4Traffic.test_flow_selection` | `--flows=2,5` runs exactly those flows |
| `T4Traffic.test_bad_flow_selection_rejected` | invalid input exits with code 2 |
| `T4Traffic.test_offered_load_is_respected` | sent packet count matches the configured rate |
| `T4Traffic.test_congestion_loss_is_explained` | congestion happens on 10 Mbps, and every loss has a recorded cause |
| `T4Traffic.test_same_seed_is_reproducible` | same seed/run gives identical results |
| `T5Analysis.test_analysis_validates_existing_results` | analysis runs and every results check passes |

## 3. Results validation (`scripts/analyze-results.py`)

Non-negative values; rx ≤ tx; PDR in 0–100%; PDR + loss = 100%; every loss
explained; app counters = FlowMonitor counters; throughput recomputes from
packet counts; seed/run recorded; filter-on policy holds; filter-off has no
blocked packets.

## Recorded outcomes

| Date | Command | Result |
|---|---|---|
| 2026-11-12 | `./ns3 run minimal-two-node` | `sent=10 received=10`, exit 0 |
| 2026-11-12 | `./ns3 run office-network` (+ filter off/on/ICMP, 10 s and 60 s) | `checks failed: 0` |
| 2026-11-12 | `python3 scripts/run-experiments.py` | 31/31 runs OK |
| 2026-11-12 | `python3 scripts/analyze-results.py` | 12/12 checks PASS, 6 figures |
| 2026-11-12 | `python3 -m unittest discover -s tests -v` | 15/15 OK (first run: 1 failure, a wrong tolerance in the test itself; fixed) |
| 2026-11-12 | Clean-clone validation (Stage 5) | see `docs/dev-log.md`, Stage 5 |
