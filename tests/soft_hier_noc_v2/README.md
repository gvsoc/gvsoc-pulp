# SoftHier NoC bridge regression

A 2×2 v2 mesh connects three legacy masters and a native v2 probe to a legacy
memory target. Two masters share one ingress. NI capacity is 2 and router FIFO
size is 1 to exercise congestion and retries.

The test checks 56 legacy transfers, including a 64-KiB unaligned write/read,
48 mixed transfers, an unmapped address and a zero-length access. The memory
target reorders beat completions and returns legacy `DENIED` on every third
access. The native probe denies a response before calling `resp_retry()`.
Every read is checked byte for byte, and a watchdog detects lost requests.

Success prints `SOFTHIER_NOC_V2_PASS`. This is a functional/protocol regression,
not an RTL timing calibration.
