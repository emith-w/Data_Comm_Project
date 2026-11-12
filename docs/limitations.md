# Limitations

## Model limitations

* **Simulation, not measurement.** Throughput and delay come from ns-3's
  models. They show how the *modelled* network behaves, not how a real
  office with real switches, NICs and operating systems would perform.
* **CSMA is a shared half-duplex bus.** ns-3's CSMA model is a single shared
  medium with no switch, so all stations on a segment share its capacity.
  Modern office LANs are switched and full-duplex, so real capacity would be higher.
* **No collision model in CSMA.** ns-3's CSMA channel only lets a device start
  when the channel is idle; it doesn't model collisions. Contention appears as
  backoff and queueing, not as collisions. This is consistent with
  `channel_drop_packets` being 0 in every run.
* **Propagation delay holds the channel** (see `experiment-design.md`). This is
  why the proposal's 2 ms value throttles the LAN.
* **Not VLANs.** The two departments are separate physical segments. There is
  no IEEE 802.1Q tagging, trunking or switch. The separation at the IP level
  (two subnets joined by a router) is the same as in the proposal.
* **No queue disc.** The default ns-3 setup (no `TrafficControlHelper`) is used,
  so queueing happens in each device's 100-packet DropTail queue. A different
  queue size or an AQM such as FqCoDel would change the delay and loss figures.

## Access-control limitations

* The filter matches IPv4 source and destination network/mask only; ports and protocols are not part of the rules.
* It is ordered first-match with an implicit permit, like a Cisco ACL, but it is **not** Cisco IOS syntax or behaviour.
* In `--filterSendIcmp` mode, the reply is ns-3's generic ICMP "unreachable", not "administratively prohibited", and it isn't rate-limited.
* Only forwarded traffic is filtered. Traffic within the Admin LAN never reaches the router, so nothing can filter it (the same as with a real router ACL).

## Traffic and measurement limitations

* Only constant-bit-rate UDP was evaluated. TCP would react to loss by slowing down, so the results would look very different.
* Throughput includes the 28-byte IP + UDP headers (FlowMonitor counts IP-layer bytes), so it reads 2.7% above the payload rate.
* Delay is measured from the sender's IP layer to the receiver's IP layer. Application processing time is not included.
* 3 RNG runs per load point show run-to-run spread (min–max), but that's too few for confidence intervals.
* **ARP failure under saturation:** in one heavily loaded run (`e4_load_high_lan100_2ms_run2`), StaffPC1 never resolved StaffPC2's MAC address. After 3 retries the ARP entry went "dead" for 100 s (ns-3 default), longer than the run, so that flow delivered nothing. This is real model behaviour, but it makes that load point's results depend strongly on the run.

## Environment limitations (for this build)

* ns-3 was built with the **release/optimized** profile, so `NS_ASSERT` and `NS_LOG` are off. The program compensates with its own explicit checks.
* The Codex CLI could not be installed in the development workspace. All building and testing was done directly; `tests/test_project.py` can be run by any tool, including Codex.
