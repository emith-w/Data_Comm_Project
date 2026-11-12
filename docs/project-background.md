# Project Background

## From Packet Tracer to ns-3

The original proposal modelled the office in **Cisco Packet Tracer**, which
emulates Cisco devices and their IOS commands. The group moved to **ns-3**
(Network Simulator 3), a free, open-source **discrete-event simulator**. In
ns-3, a program written in C++ builds nodes, links and protocol stacks. Every
packet transmission, reception and timer then becomes an event on a simulated
clock. This gives exact, repeatable measurements of throughput, loss and delay,
which Packet Tracer doesn't provide. The trade-off is that ns-3 has no Cisco
command line.

## How each proposal objective was adapted

| Proposal (Packet Tracer) | This project (ns-3.49) |
|---|---|
| 4 PCs, 1 router, switches | 4 host `Node`s, 1 router `Node`, 2 `CsmaChannel`s |
| VLAN 10 (Admin), VLAN 20 (Staff) | Two separate CSMA segments, one per department (no 802.1Q tagging; see limitations) |
| Router-on-a-stick / inter-VLAN routing | A router node with one interface per segment; `Ipv4GlobalRouting` computes routes |
| `ip address` commands | `Ipv4::AddAddress` with the proposal's addresses, read back and checked in code |
| Extended ACL on the router | `AclFilterRouting`: a filter in the router's IPv4 forwarding path (`docs/access-control.md`) |
| `ping` tests | UDP constant-bit-rate flows with per-flow counters |
| Visual inspection | FlowMonitor + trace counters, CSV export, graphs |

## Key terms

* **Subnet:** a block of IP addresses sharing a network prefix, e.g. 192.168.10.0/24 (the first 24 bits are the network part).
* **Router / IP forwarding:** a node that receives packets addressed to *other* hosts and sends them on towards their destination.
* **CSMA:** Carrier Sense Multiple Access, the shared-medium access method of classic Ethernet. A station listens and only transmits when the channel is idle.
* **ACL:** access control list, a router rule set that permits or denies packets based on header fields.
* **Throughput:** bits successfully delivered per second.
* **PDR (packet delivery ratio):** received packets ÷ sent packets.
* **End-to-end delay:** time from a packet leaving the sender's IP layer to arriving at the receiver's.
* **FlowMonitor:** the ns-3 module that records per-flow packet counts and delays.
