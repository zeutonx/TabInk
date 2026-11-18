# tools/ — Measurement and debugging

| Path | Purpose |
|---|---|
| `latency-benchmark/` | Collect per-stage timestamps and compute p50/p95/p99/max ([test-matrix.md §3](../docs/test-matrix.md#3-latency-report-p50--p95--p99--max-500-strokes)) |
| `packet-replay/` | Replay recorded pen logs into the host and check that the sample count is preserved |
| `calibration/` | 9×9 grid target and error analysis ([test-matrix.md §4](../docs/test-matrix.md#4-calibration-99-grid-target-median-1-px-p95-2-tablet-px)) |
