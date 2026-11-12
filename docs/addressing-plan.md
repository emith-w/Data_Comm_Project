# IPv4 Addressing Plan

These addresses come from the original proposal and are kept unchanged.

| Subnet | Network | Mask | Usable hosts | Gateway |
|---|---|---|---|---|
| Admin | 192.168.10.0/24 | 255.255.255.0 | .1 – .254 | 192.168.10.1 |
| Staff | 192.168.20.0/24 | 255.255.255.0 | .1 – .254 | 192.168.20.1 |

| Node | ns-3 interface | Address |
|---|---|---|
| Admin PC1 | 1 | 192.168.10.10 |
| Admin PC2 | 1 | 192.168.10.11 |
| Staff PC1 | 1 | 192.168.20.10 |
| Staff PC2 | 1 | 192.168.20.11 |
| Router | 1 (Admin LAN) | 192.168.10.1 |
| Router | 2 (Staff LAN) | 192.168.20.1 |

(Interface 0 on every node is the loopback, 127.0.0.1.)

## How the addresses are assigned

`Ipv4AddressHelper` hands out addresses one after another (.1, .2, .3 …),
which can't produce the plan's .1 / .10 / .11 pattern. So
`AssignAddress()` in `office-network.cc` adds each interface explicitly with
`Ipv4::AddInterface`, `Ipv4::AddAddress` and `Ipv4::SetUp`. The program then
**reads every address back** and checks it against the plan
(`CHECK PASS: AdminPC1 has address 192.168.10.10`, …). Nothing depends on
node creation order without being checked.

## Routing

`Ipv4GlobalRoutingHelper::PopulateRoutingTables()` builds routes from the
whole topology. The routing tables actually installed are saved in each run's
`routing-tables.txt`. For example, the hosts in the Admin subnet get:

```
192.168.10.0    0.0.0.0         255.255.255.0   U     (directly connected)
192.168.20.0    192.168.10.1    255.255.255.0   UG    (via the router)
```

The program also asks each host's routing protocol for a route and checks the
gateway it returns. For example, StaffPC1 → 192.168.10.10 goes via
192.168.20.1. It also checks that IP forwarding is enabled on both router interfaces.
