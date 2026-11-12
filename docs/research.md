# Stage 1 — Research and Environment Investigation

This document records what was checked **before** writing simulation code: the
development environment, the ns-3 release we picked, the APIs we confirmed in
that release's source code, and the main technical risks.

## 1. Development environment (inspected 2026-11-12)

| Item | Found | Notes |
|---|---|---|
| OS | Ubuntu 24.04.5 LTS (x86_64) | Linux workspace |
| C++ compiler | g++ 13.3.0 (also clang++) | C++20 capable, required by ns-3.49 |
| CMake | 3.28.3 | ns-3.49 needs ≥ 3.13 |
| Build tool | Ninja | used by `./ns3` |
| Python | Python 3 with matplotlib 3.11 and pandas 3.0 | used for automation and plots |
| Git | available | version control |
| ns-3 | **not pre-installed** | downloaded (see below) |
| Codex CLI | **not available** | package installs are blocked in this workspace; all builds/tests were run directly instead |

## 2. ns-3 version selection

* The official release tags were listed from the official GitHub mirror of
  ns-3-dev (`github.com/nsnam/ns-3-dev-git`). The newest release tag found was
  **ns-3.49**, so that is the version used.
  (`nsnam.org` and `gitlab.com` were blocked by this workspace's network proxy;
  the GitHub mirror holds the same tagged releases.)
* Download command used:
  ```bash
  git clone --depth 1 --branch ns-3.49 https://github.com/nsnam/ns-3-dev-git.git ns-3.49
  ```
* Only the modules this project needs were enabled to keep build time down:
  ```bash
  ./ns3 configure --enable-modules="core;network;internet;csma;applications;flow-monitor" \
                  --disable-examples --disable-tests --disable-python
  ./ns3 build
  ```
  ns-3 automatically adds the dependencies of those modules
  (`bridge`, `csma-layout`, `internet-apps`, `point-to-point`, `stats`, `traffic-control`).

## 3. APIs confirmed in the ns-3.49 source

Every API below was checked in the 3.49 source tree, not taken from memory.

| Need | API (ns-3.49) | Where verified |
|---|---|---|
| Nodes | `NodeContainer` | `src/network/helper/node-container.h` |
| LAN segments | `CsmaHelper` with attributes `DataRate`, `Delay` | `src/csma/helper` |
| IP stack | `InternetStackHelper::SetRoutingHelper()` | `src/internet/helper` |
| Routing | `Ipv4GlobalRoutingHelper::PopulateRoutingTables()`, `Ipv4ListRoutingHelper` | `src/internet/helper` |
| Specific addresses | `Ipv4::AddInterface`, `Ipv4::AddAddress`, `Ipv4::SetUp` | `src/internet/model/ipv4.h` |
| UDP traffic | `UdpClient` (`MaxPackets`, `Interval`, `RemoteAddress`, `RemotePort`, `PacketSize`), `UdpServer` (`GetReceived()`, `GetLost()`), `UdpClient::GetTotalTx()` | `src/applications/model/udp-client.cc`, `udp-server.h` |
| Flow statistics | `FlowMonitorHelper`, `FlowMonitor::CheckForLostPackets()`, `GetFlowStats()`, `Ipv4FlowClassifier::FindFlow()` | `src/flow-monitor` |
| Drop evidence | `Ipv4L3Protocol` trace source `Drop` with reason `DROP_ROUTE_ERROR` | `src/internet/model/ipv4-l3-protocol.h` |

## 4. Packet filtering — the main technical question

ns-3 has no Cisco ACL command set. We looked at three possible hooks:

1. **`Ipv4PacketFilter` / `PacketFilter` (traffic-control module).**
   Despite the name, these only *classify* packets into queue-disc bands
   (`DoClassify()` returns a band number or `PF_NO_MATCH`). They don't
   permit or deny packets, so they aren't an ACL.
2. **`Ipv4L3Protocol` trace sources (`UnicastForward`, `Rx`, …).**
   Traces only *observe* packets; they can't stop one. Not usable for
   enforcement, but useful as evidence.
3. **A custom `Ipv4RoutingProtocol` placed first in `Ipv4ListRouting` on the router. ✅ Chosen.**
   In `src/internet/model/ipv4-list-routing.cc` (3.49), `RouteInput()`:
   * delivers packets addressed to the router itself first, then
   * asks each routing protocol **in priority order** to handle a packet that
     must be forwarded. The first protocol that returns `true` "owns" the packet.

   So a small filter protocol with the highest priority sees **every IPv4
   packet the router is asked to forward**. For a DENY it calls the
   router's own error callback (`ecb`). In `Ipv4L3Protocol::RouteInputError()`
   that callback fires the `Drop` trace (reason `DROP_ROUTE_ERROR`) and
   sends an ICMP "unreachable" back to the sender, the same way a real router
   reports a filtered packet. For a PERMIT it returns `false`, so
   `Ipv4GlobalRouting` forwards the packet normally.

   This is the closest ns-3 equivalent of a Cisco extended ACL applied
   on the router. It works in the network layer's forwarding path and doesn't
   touch the sending or receiving applications.

## 5. Principal technical risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| Filter hook doesn't see packets | Objective 4 would fail | Prototype in Stage 3 and count drops with the router's `Drop` trace, independently of the filter's own counter |
| Non-contiguous addresses (.1, .10, .11) | `Ipv4AddressHelper` assigns sequentially | Assign addresses explicitly with `Ipv4::AddAddress`, then read them back and check in code |
| Load levels too small to matter | 1/5/10 Mbps is far below 100 Mbps CSMA | Run the load experiment on 100 Mbps **and** on a justified 10 Mbps bottleneck LAN (see experiment design) |
| FlowMonitor counts in-flight packets as lost | Wrong loss/PDR | Stop applications 2 s before simulation end, call `CheckForLostPackets()`, and cross-check against `UdpServer` counters |
| Policy drops confused with congestion loss | Wrong conclusions | Count policy drops separately (filter counter + `Drop` trace) and report `blocked_packets` as its own CSV column |
| Port/flow mix-ups | Flows merge in output | Give each flow a unique destination port; FlowMonitor's 5-tuple then separates them |
