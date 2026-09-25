# SoftHier data NoC backends

`legacy` remains the default. Select `floonoc_v2` in the architecture class:

```python
self.data_noc_backend = 'floonoc_v2'
self.noc_outstanding = 64             # Outstanding bursts per NI port
self.noc_link_width = 1024            # Wide link, bits
self.noc_narrow_link_width = 64       # Request/response link, bits
self.noc_router_input_queue_size = 2  # Router input FIFO setting
```

Alternatively, export `SOFTHIER_DATA_NOC=floonoc_v2` for **both build and run**.
The environment setting overrides the architecture field. Use `legacy` to
restore the original data NoC. The generated chip configuration records the
selected backend and all mesh parameters.

The v2 wrapper places explicit protocol bridges around `pulp/floonoc_v2`.
The cluster's sparse iDMA, TCDM, HBM controller, DRAMSys configuration and SDK
instructions keep their existing interfaces. `sync_bus` stays on the legacy
model. Each NI has a single v2 initiator; the legacy ingress face retains the
response port of every upstream requester, including the shared cluster-0
preloader and virtual-interconnect inputs.

Supported: unicast reads/writes, local and remote TCDM, and unaliased HBM nodes
on any edge (`hbm_node_aliase=1`). DMA collectives fail explicitly on this backend;
use `legacy` for collectives, edge aliases and other legacy-only operations.
The original collective behavior remains enabled with `legacy`.

## Timing and buffers

- Reads split at 4-KiB boundaries; writes split at wide-link beat boundaries.
- The target bridge splits reads into wide-link beats, preserving the HBM
  controller's address striping. It issues at most one beat per cycle and
  limits buffered beats and accepted requests to `noc_outstanding`.
- A source bridge issues at most one request/beat per cycle. The NI enforces
  its outstanding limit. Legacy requests retain their data buffers until all
  fragments complete; every read response is consumed into that storage.
- Translation uses clock events. Existing legacy latency annotations elapse
  before completion, and zero-latency legacy target responses are deferred by
  one cycle. These bridge cycles are included in benchmark results.
- Responses preserve per-request byte order, including when target beats
  complete out of order. Request and response retries are serviced synchronously.

V2 routes read payload through the wide return network. The legacy NoC completes
reads through callbacks. Queue capacities and outstanding counts have different
meanings between the two models; matching their numeric values is not an RTL
calibration. The v2 defaults above require calibration against the intended
SoftHier RTL before making a cycle-accuracy claim.

## Verification

`pulp/tests/soft_hier_noc_v2` checks shared inputs, high physical addresses,
64-KiB unaligned transfers, small queues, mixed reads/writes, both retry
protocols, invalid addresses and out-of-order target completion. It is included
in the PULP test set. From that directory, after sourcing GVSoC's environment:

```sh
make build GVSOC_ROOT=../../..
make run GVSOC_ROOT=../../..
```

Trace records `NOC_V2_INJECT`, `NOC_V2_HOP` and `NOC_V2_EJECT` identify each flit
and retain 64-bit addresses. The SDK benchmark analysis audits requests and
return payload independently and plots their separate mesh routes.
