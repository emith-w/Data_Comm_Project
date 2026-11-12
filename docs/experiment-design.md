# Experiment Design

All experiments use UDP constant-bit-rate traffic, 1024-byte payloads, a 60 s
simulation (traffic from ~1 s to 58 s), and seed 1. They are run by
`scripts/run-experiments.py`.

| Run id | Experiment | Flows | Load / flow | LAN | Filter |
|---|---|---|---|---|---|
| `e1_basic_connectivity` | 1. Basic connectivity | 1, 2 (same dept.) | 1 Mbps | lan100 | off |
| `e2_interdepartmental_routing` | 2. Interdepartmental routing | 3–6 | 1 Mbps | lan100 | off |
| `e3_access_control_off` | 3. Access control (before) | 1–6 | 1 Mbps | lan100 | off |
| `e3_access_control_on` | 3. Access control (after) | 1–6 | 1 Mbps | lan100 | **on** (+ICMP) |
| `e4_load_<level>_<lan>_run<n>` | 4. Traffic load | 1–6 | 1 / 5 / 10 Mbps | lan100, lan10, lan100_2ms | off |

Experiment 4 uses 3 independent RNG runs (`--run=1..3`) per point: 3 LANs × 3 loads × 3 runs = 27 runs.
Experiment 5 (performance comparison) is the analysis of all of the above, in
`results/summary.md` and the figures.

## Parameter choices and why

| Parameter | Value | Reason |
|---|---|---|
| CSMA data rate | 100 Mbps | Fast Ethernet, typical of a small office (proposal value) |
| CSMA delay | **500 ns** | ≈ 100 m of copper at 2×10⁸ m/s, the maximum length of an Ethernet run |
| Simulation time | 60 s | Proposal value. At 1 Mbps it gives ~7,000 packets per flow, plenty for stable averages |
| Transport | UDP | No retransmission or congestion control, so delivery and loss reflect the network directly |
| Packet size | 1024 B | Large data packets, below the 1500 B Ethernet MTU (no fragmentation) |
| Start jitter | 0–10 ms | Stops all flows starting in the same instant and gives the RNG run number something to vary |
| Drain time | 2 s | Queued packets can arrive before FlowMonitor decides what's lost |

## Why three LAN profiles

1. **lan100: 100 Mbps, 500 ns (realistic office).** The proposal's loads
   (1/5/10 Mbps per flow) were tested here first. With all six flows, each
   segment carries at most ~50–60 Mbps, so **nothing changes between load
   levels** (100% delivery, ~0.2 ms delay). The brief asks us to confirm whether
   the loads produce meaningful differences, and on this LAN they don't.
2. **lan10: 10 Mbps, 500 ns (justified bottleneck).** 10BASE-T is a real
   Ethernet standard and still appears on old equipment. At 5 and 10 Mbps
   per flow the offered load exceeds what each segment can carry, so the
   load experiment shows real queueing delay and loss.
3. **lan100_2ms: 100 Mbps, 2 ms (the proposal's suggested delay).** In
   `src/csma/model/csma-channel.cc` (ns-3.49), the channel stays in the
   `PROPAGATING` state, and is therefore busy, until the propagation delay has
   passed. One 1024-byte frame (1082 B on the wire) takes 0.087 ms to transmit
   plus 2 ms of propagation, so each segment carries at most ~480 frames/s
   (≈ 3.9 Mbps of payload) whatever its data rate. 2 ms also corresponds to
   ~400 km of cable, not an office. This series is kept to show the effect
   of that parameter openly, rather than dropping it without comment.

## Separating policy drops from congestion

Each flow's missing packets are split into blocked by the filter, transmit
queue full, CSMA channel drop, and ARP failure. Each run checks that these add
up exactly to `tx − rx`, and Experiment 3 runs at a load (1 Mbps on 100 Mbps)
where congestion loss is zero.
