# Development Log

Each entry records what was done, the commands used, and the outcome.
Only results that were actually executed are recorded here.

## Stage 1 — Research & environment (2026-11-12)

* Checked the tools: g++ 13.3, CMake 3.28, Ninja, Python 3 (matplotlib, pandas), Git. ns-3 was not installed.
* Codex CLI isn't installed and can't be installed here (package installs are blocked). All compiling and testing is done directly.
* `nsnam.org` and `gitlab.com` are blocked by the workspace proxy, so ns-3.49 was cloned from the official GitHub mirror:
  `git clone --depth 1 --branch ns-3.49 https://github.com/nsnam/ns-3-dev-git.git`
* Configured with only the required modules (see `docs/research.md`).
* Researched the packet-filter hook in the 3.49 source. Chose a custom `Ipv4RoutingProtocol` at the top of the router's `Ipv4ListRouting`.
* GitHub: the workspace's GitHub login is invalid and the account isn't linked to Claude, so nothing is pushed yet. Commits are kept locally, one per stage.

## Stage 2 — Minimal simulation and office topology (2026-11-12)

* Built ns-3.49 (`./ns3 build`, exit 0). Note: the cached build profile is `release`/optimized, so `NS_ASSERT` is off. The simulation therefore does its own explicit checks (`CHECK PASS/FAIL` lines) and exits non-zero on any failure.
* Linked sources into ns-3: `scripts/link-into-ns3.sh /home/claude/ns3/ns-3.49`
* `./ns3 run minimal-two-node` → `sent=10 received=10`, exit 0 ✅
* Built the 5-node office network (`simulation/office-network/`). Addresses are set explicitly and global routing is used. All 14 structural/routing checks pass (4 hosts, 3 router interfaces, separate CSMA channels, every address, IP forwarding, routes via the correct gateway).
* **Finding — the CSMA propagation delay.** With the proposal's 2 ms delay, flows at only 1 Mbps each lost 15–53% of packets, with average delays of 430–720 ms (10 s run). In `src/csma/model/csma-channel.cc` the channel stays in state `PROPAGATING` (busy) until the delay has passed. So each ~1 KB frame holds the shared segment for ≈ 2.09 ms, which caps a LAN at ≈ 480 frames/s whatever its data rate.
  Decision: the default delay is now **500 ns** (≈ 100 m of cable). `--csmaDelay=2ms` stays available and is used as a comparison scenario in Stage 4.
* 60 s baseline (`results/stage2-baseline/`): all 6 flows tx = rx (≈ 6958 packets each), 100% delivery, 0.087–0.174 ms average delay. For the 4 cross-department flows, router rx = router forwarded = packets delivered. ✅
* Throughput is 1.0273 Mbps for 1 Mbps offered because FlowMonitor counts IP+UDP headers (1052 / 1024 bytes).
* Replaced the deprecated `UdpL4Protocol::PROT_NUMBER` (deprecated in 3.49) with `iana::internetprotocolnumbers::UDP`. The build now has no warnings.

## Stage 3 — Access-control filter (2026-11-12)

* Added `AclFilterRouting` (`simulation/office-network/access-control.h`) at priority 100 in the router's `Ipv4ListRouting`. New flags: `--enableFilter`, `--filterSendIcmp`.
* Added router `Drop` trace counting and automatic policy checks (blocked == router rx, forwarded == 0, destination rx == 0 for denied flows; blocked == 0 everywhere else).
* 10 s tests, all `checks failed: 0`:
  * `--enableFilter=false` → all 6 flows 100% delivered, 0 blocked ✅
  * `--enableFilter=true` → flows 3 and 5 (Staff→AdminPC1) 0 received, 854/854 blocked, 0 forwarded. Flows 1, 2, 4 and 6 at 100% ✅
  * `--enableFilter=true --filterSendIcmp=true` → same, and the router Drop trace counted every blocked packet ✅
* 60 s evidence saved in `results/stage3-filter-on/` and `results/stage3-filter-off/` (0 failed checks in both).
* Design and evidence: `docs/access-control.md`.
