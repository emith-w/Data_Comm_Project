# Results

Everything in this folder was produced by real ns-3.49 runs. Regenerate it with:

```bash
python3 scripts/run-experiments.py && python3 scripts/analyze-results.py
```

| Path | Content |
|---|---|
| `all-results.csv` | one row per flow per run (180 rows), all runs merged |
| `summary.md` | tables for every experiment and the results-validation outcome |
| `run-manifest.json` | ns-3 version, project git commit, exact command and exit code of every run |
| `runs/<run_id>/flows.csv` | per-flow results of one run |
| `runs/<run_id>/metadata.txt` | all parameters of that run (rate, delay, load, seed, run, filter, …) |
| `runs/<run_id>/validation.txt` | the `CHECK PASS/FAIL` lines of that run |
| `runs/<run_id>/routing-tables.txt` | every node's routing tables (and the filter rules) at t = 1 s |
| `runs/<run_id>/flowmon.xml` | raw FlowMonitor output |
| `runs/<run_id>/console.txt` | everything the run printed |
| `stage2-baseline/`, `stage3-filter-*` | evidence runs saved during Stages 2 and 3 |

## CSV columns

| Column | Meaning |
|---|---|
| `run_id`, `experiment_name`, `lan_profile`, `lan_label` | which run (added by the runner) |
| `experiment_id`, `flow_id`, `source`, `destination`, `source_ip`, `destination_ip`, `dst_port` | which flow |
| `offered_load_mbps`, `filter_enabled`, `csma_rate`, `csma_delay`, `packet_size`, `seed`, `run`, `duration_s` | parameters (`duration_s` = traffic window used for throughput) |
| `app_tx_packets`, `app_rx_packets` | UdpClient / UdpServer counters |
| `tx_packets`, `rx_packets`, `lost_packets` | FlowMonitor counters |
| `blocked_packets` | dropped by the access-control filter |
| `queue_drop_packets`, `channel_drop_packets`, `arp_drop_packets` | congestion-related drops by cause |
| `router_rx_packets`, `router_forwarded_packets`, `router_drop_trace_packets` | router trace counters |
| `throughput_mbps` | delivered IP-layer bits / traffic window (includes 28 B IP+UDP headers) |
| `packet_delivery_ratio`, `packet_loss_ratio` | % |
| `average_delay_ms`, `average_jitter_ms` | FlowMonitor, received packets only (empty if nothing was received) |
