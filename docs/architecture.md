# Architecture

The project is deliberately small: one ns-3 program, two headers and two
Python scripts.

## Source files

| File | Responsibility |
|---|---|
| `simulation/office-network/network-config.h` | `SimConfig` (every tunable parameter), the addressing plan, the six-flow table, `--flows` parsing |
| `simulation/office-network/access-control.h` | `AclFilterRouting`, the router packet filter |
| `simulation/office-network/office-network.cc` | builds the network, installs traffic and measurement, runs checks, writes results |
| `simulation/minimal-two-node.cc` | the Stage 2 sanity check |
| `scripts/run-experiments.py` | builds, runs every experiment, merges CSVs, writes the manifest |
| `scripts/analyze-results.py` | validates the CSV, draws figures, writes `results/summary.md` |
| `tests/test_project.py` | automated tests that run the real simulation |

## What `office-network.cc` does, in order

1. **Parse options** into `SimConfig` (`CommandLine`). Invalid values exit with code 2.
2. **Seed the random generator** (`RngSeedManager::SetSeed/SetRun`).
3. **Nodes:** 4 hosts + 1 router.
4. **Channels:** two `CsmaHelper` installs. Admin LAN = {AdminPC1, AdminPC2, router}; Staff LAN = {StaffPC1, StaffPC2, router}.
5. **IP stacks** (`InternetStackHelper`). This gives each node `Ipv4ListRouting` with static (priority 0) and global (−10) routing.
6. **Addresses** set explicitly (see `addressing-plan.md`).
7. **Global routing** tables populated.
8. **Access-control filter** added to the router's list routing at priority 100. It is always installed and only switched on by `--enableFilter`.
9. **Structural checks:** node count, interfaces, separate channels, addresses, forwarding, routes, filter position.
10. **Evidence traces:**
    * router `Ipv4L3Protocol`: `Rx`, `UnicastForward`, `Drop`
    * every node: `TrafficControlLayer::TcDrop`, `ArpL3Protocol`/`ArpCache` `Drop`
    * every CSMA device: `MacTxDrop`, `PhyTxDrop`

    Each is counted per flow, keyed on (source, destination, UDP port).
11. **Traffic:** for each selected flow, a `UdpServer` on the destination and a
    `UdpClient` on the source. The client sends one packet every
    `packetSize × 8 / loadMbps` seconds, starting at 1 s plus 0–10 ms of random
    jitter, and stopping `drainTime` seconds before the end.
12. **FlowMonitor** on all nodes.
13. **Run** the simulation, then `CheckForLostPackets(drainTime)`.
14. **Per-flow results:** FlowMonitor stats are matched to each flow by its 5-tuple.
    Then the app counters, filter counters and trace counters are joined, and the
    metrics computed.
15. **Policy and consistency checks:** see `tests/test-plan.md`.
16. **Write** `flows.csv`, `metadata.txt`, `validation.txt`, `routing-tables.txt`, `flowmon.xml`. Exit code 0 only if every check passed.

## Metric definitions

| Metric | Formula | Source |
|---|---|---|
| `tx_packets`, `rx_packets` | packets sent / received at the IP layer | FlowMonitor (cross-checked against `UdpClient`/`UdpServer`) |
| `throughput_mbps` | rxBytes × 8 / (active duration × 10⁶) | FlowMonitor `rxBytes` **includes the 20 B IP + 8 B UDP headers**; the active duration is the same fixed traffic window for every flow (57 s by default) |
| `packet_delivery_ratio` | rx / tx × 100 | FlowMonitor |
| `packet_loss_ratio` | (tx − rx) / tx × 100 | FlowMonitor |
| `average_delay_ms` | delaySum / rx | FlowMonitor (IP layer, sender → receiver) |
| `average_jitter_ms` | jitterSum / (rx − 1) | FlowMonitor |
| `blocked_packets` | packets denied by the filter | `AclFilterRouting` counter |
| `queue_drop_packets` | dropped because a transmit queue was full | `TcDrop` + `MacTxDrop` |
| `channel_drop_packets` | CSMA transmission aborted / backoff limit | `PhyTxDrop` |
| `arp_drop_packets` | ARP pending queue full or request timed out | ARP `Drop` traces |

Each run checks the identity `tx − rx = blocked + queue + channel + arp`.

## FlowMonitor notes

* It classifies only TCP and UDP over IPv4/IPv6. ICMP (for example the filter's unreachable messages) is not counted as a flow.
* It counts a packet as "lost" only after no arrival for a set time. Packets still queued at the end would be miscounted, so traffic stops 2 s early and `CheckForLostPackets(2 s)` is called.
* Flows are separated by 5-tuple. Every flow has its own destination port, so flows can't merge.
* A packet blocked by the filter shows up in FlowMonitor as lost. The `blocked_packets` column is what separates policy drops from congestion.
