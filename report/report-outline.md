# Report Outline — Design and Testing of a Small Office Network Using ns-3

Target length: 8–12 pages. Each section lists what to write and where the
material is in this repository. Use only the measured numbers in
`results/summary.md`; don't round them into claims they don't support.

## Abstract (≈ ½ page)
Problem, approach (ns-3.49 simulation of a two-subnet office with a router
filter), the main measured results (filter blocks 100% of Staff→AdminPC1 and
nothing else; load behaviour on 100 vs 10 Mbps LANs), and one sentence on limitations.

## 1. Introduction (≈ ½ page)
Why small offices separate departments; why access control matters for
sensitive Admin data; why simulate. Outline of the report.

## 2. Background research (≈ 1–1½ pages)
* Subnetting and IPv4 routing; what a router ACL does (`docs/project-background.md`)
* CSMA/Ethernet shared media; why ns-3 instead of Packet Tracer
* ns-3 building blocks used: CSMA, Internet stack, global routing, UDP apps, FlowMonitor
* How packet filtering can be done in ns-3, and why queue-disc packet filters don't fit (`docs/research.md` §4)
* References: ns-3 documentation and source for release 3.49; course textbook; IEEE 802.3

## 3. Problem statement (≈ ¼ page)
The research question from `README.md` §1.

## 4. Objectives (≈ ¼ page)
The six objectives from the project brief, each mapped to the experiment that tests it.

## 5. Network design (≈ 1 page)
Topology diagram, addressing table (`docs/addressing-plan.md`), routing
method, filter policy table (`docs/access-control.md`), parameter choices and
reasons (`docs/experiment-design.md`).

## 6. Methodology (≈ 1 page)
Traffic model (CBR UDP, 1024 B, per-flow ports), metrics and formulas
(`docs/architecture.md`), measurement window and drain time, FlowMonitor's
limitations, seeds and runs, how policy drops are separated from congestion,
and validation (in-simulation checks + tests).

## 7. Implementation (≈ 1–1½ pages)
Program structure (`docs/architecture.md`); the filter's place in
`Ipv4ListRouting::RouteInput` (include a short excerpt of `RouteInput()` from
`access-control.h`); explicit address assignment; evidence traces; automation scripts.

## 8. Experiments (≈ ½ page)
Table of the 31 runs (`docs/experiment-design.md`), including why three LAN profiles were needed.

## 9. Results (≈ 2 pages)
* Exp. 1–2 tables from `results/summary.md`
* Exp. 3: Figure 5 plus the filter-on table (blocked / router rx / forwarded)
* Exp. 4: Figures 1–4 (throughput, PDR, delay, loss vs load) and the summary table
* Figure 6: drop causes

## 10. Discussion (≈ 1 page)
Answer the brief's questions with the data:
* Does routing allow communication between departments? (Exp. 2)
* Does filtering restrict the protected computer, and does permitted traffic keep working? (Exp. 3)
* How does offered load affect throughput and delay? (saturation on 10 Mbps; no effect on 100 Mbps)
* Under what conditions are packets dropped? (full transmit queues; ARP failure under saturation)
* The 2 ms propagation-delay finding, and what it says about choosing simulation parameters.

## 11. Limitations (≈ ½ page)
From `docs/limitations.md`.

## 12. Conclusion and future work (≈ ½ page)
Summary; possible extensions: TCP traffic, switched point-to-point links instead
of CSMA, queue discs (FqCoDel), port-based ACL rules, more runs for confidence intervals.

## References
List the ns-3.49 documentation pages and source files actually cited, the
ns-3 release notes, and the course text. Don't cite sources nobody in the group has read.

## Appendix
Build/run commands (`README.md` §4–11), test outcomes (`tests/test-plan.md`), development log (`docs/dev-log.md`).

## Suggested split among the four members
| Member | Sections |
|---|---|
| Jay Patel | 1, 2, 3, 4 |
| Emith Weligamage | 5, 7 |
| Jenish Prajapati | 6, 8, 9 |
| Muntha Rahman | 10, 11, 12, abstract, references |
