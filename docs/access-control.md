# Access Control (Packet Filtering) Design

The original proposal used a Cisco extended ACL on the router to stop the Staff
department from reaching Admin PC1. ns-3 can't run Cisco commands, so the same
policy is built as an IPv4 packet filter inside the simulated router's
**forwarding path**.

## Policy

| # | Source | Destination | Action |
|---|---|---|---|
| 1 | 192.168.20.0/24 (Staff) | 192.168.10.10/32 (Admin PC1) | **DENY** |
| 2 | 192.168.20.0/24 (Staff) | 192.168.10.11/32 (Admin PC2) | PERMIT |
| 3 | any | any | PERMIT (implicit) |

Rules are checked from top to bottom and the **first match wins**, as in a Cisco ACL.

## Where the filter runs

`simulation/office-network/access-control.h` defines `AclFilterRouting`, a
subclass of `ns3::Ipv4RoutingProtocol`. It is added to the router's
`Ipv4ListRouting` with priority **100**, above `Ipv4StaticRouting` (0) and
`Ipv4GlobalRouting` (−10).

When a packet arrives at the router, `Ipv4L3Protocol::Receive()` removes the
IP header and calls `Ipv4ListRouting::RouteInput()`. That function:

1. delivers the packet locally if it is addressed to the router itself;
2. otherwise offers it to each routing protocol in priority order.

So the filter is asked about **every IPv4 packet the router would forward**,
before any route lookup happens.

* **Permit:** `RouteInput()` returns `false`, so global routing forwards the packet normally.
* **Deny:** `RouteInput()` returns `true` without forwarding, so the packet is dropped
  inside the router. With `--filterSendIcmp=true`, the router's own error
  callback is called as well. That fires the router's `Drop` trace (reason
  `DROP_ROUTE_ERROR`) and sends an ICMP "unreachable" back to the sender.

Packets between two hosts in the same department never reach the router
(they are delivered on the LAN), so the filter can't affect them. This
matches a real router ACL.

## Fields inspected

* IPv4 **source address**, matched against a network/mask
* IPv4 **destination address**, matched against a network/mask
* The UDP destination port is read **only to count** blocked packets per flow. It is never used to decide.

## Counting and evidence

Three independent counters show that packets are dropped by the filter
**at the router**. They don't rely on simply seeing no traffic at the destination:

| Counter | Source | Expected for a denied flow |
|---|---|---|
| `router_rx_packets` | router `Ipv4L3Protocol` **Rx** trace | = packets sent |
| `blocked_packets` | filter's own per-flow counter | = `router_rx_packets` |
| `router_forwarded_packets` | router **UnicastForward** trace | 0 |
| `router_drop_trace_packets` | router **Drop** trace (ICMP mode) | = `blocked_packets` |
| `rx_packets` | FlowMonitor at the destination | 0 |

FlowMonitor also reports blocked packets as `lost_packets`, because it only
sees that they never arrived. The separate `blocked_packets` column is what
tells **policy drops** apart from **congestion loss**.

## Enabling and disabling

```bash
./ns3 run "office-network --enableFilter=false"                        # baseline
./ns3 run "office-network --enableFilter=true"                         # silent drop
./ns3 run "office-network --enableFilter=true --filterSendIcmp=true"   # drop + ICMP + Drop trace
```

The filter object is **always installed**. The flag only switches it on, so
the baseline and filtered runs use the same code path. When it's disabled,
`RouteInput()` returns `false` straight away.

## Measured evidence (60 s, 1 Mbps per flow, filter + ICMP on)

Taken from `results/stage3-filter-on/flows.csv`:

| Flow | rx | blocked | router rx | router forwarded | router Drop trace |
|---|---|---|---|---|---|
| 1 AdminPC1→AdminPC2 | 6958 | 0 | 0 | 0 | 0 |
| 2 StaffPC1→StaffPC2 | 6958 | 0 | 0 | 0 | 0 |
| 3 StaffPC1→AdminPC1 | **0** | **6958** | 6958 | **0** | 6958 |
| 4 StaffPC1→AdminPC2 | 6957 | 0 | 6957 | 6957 | 0 |
| 5 StaffPC2→AdminPC1 | **0** | **6958** | 6958 | **0** | 6958 |
| 6 StaffPC2→AdminPC2 | 6958 | 0 | 6958 | 6958 | 0 |

With the filter off (`results/stage3-filter-off/`), all six flows deliver every packet and `blocked_packets` is 0.

## Limitations

* The filter matches on addresses only (like a standard ACL extended with a
  destination). Protocol/port matching would be easy to add to `AclRule` but
  isn't needed for this policy.
* The ICMP sent in `--filterSendIcmp` mode is ns-3's generic "unreachable"
  from `Ipv4L3Protocol::RouteInputError()`, not Cisco's "administratively
  prohibited" code. It is also not rate-limited.
