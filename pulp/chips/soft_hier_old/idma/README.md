# Optional sparse gather in the private SoftHier iDMA

This model provides the same sparse-gather programming interface and packed-index
decomposition as `pulp/pulp/idma`, while retaining the private SoftHier backend.
The extension is disabled by default. Existing cluster instantiations and wiring
keep their previous behavior.

## Enable and connect

Pass `gather_enable=True` to this directory's `SnitchDma` constructor and bind
`dma.o_INDEX(...)` to an IO-v1 memory interface. Index requests carry **absolute
addresses**; use a router that removes the local-memory base when connecting a
memory with relative addresses. The index interface issues aligned 8-byte reads,
with at most one read pending. It supports synchronous, pending, and denied
requests; a denied request is retained until its response.

The executable integration example is
[`targets/soft_hier_old_dma_isa_test.py`](../../../../targets/soft_hier_old_dma_isa_test.py).
The legacy Snitch ISA also decodes `dmidx`; the shared ISS handler already exists
in the previously implemented core submodule.

## Programming

| Instruction | Gather setting |
| --- | --- |
| `dmsrc` / `dmdst` | 64-bit source / destination base |
| `dmstr` | Source row stride / destination row stride, in bytes |
| `dmrep` | Number of selected rows |
| `dmidx rs1, width` | RV32 index-list address and width code: 0/1/2/3 = 8/16/32/64 bits |
| `dmcpyi rd, size, 4` | Launch gather; `size` is bytes copied per selected row |

For row `i`, the model copies `size` bytes from
`src + unsigned(index[i]) * src_stride` to `dst + i * dst_stride`.
Indices are little-endian and packed into 64-bit words. The index list must be
8-byte aligned and the source stride must be a nonzero power of two. Pad the
final index word to 8 readable bytes. Gather takes precedence over the 2D flag
when both config bits are set (`config=6`). Empty gathers complete without memory
traffic, with completion status held behind any older unfinished transfer.

Use **`dmcpyi`** for gather here: the private model's `dmcpy` encoding selects a
legacy collective type. Its meaning and `dmmask` remain unchanged. Gather sends
unicast data requests with zero collective metadata and preserves the programmed
mask for a later collective. Descriptor settings are captured at launch, so
programming another queued gather does not change the first one.

## Compatibility and scope

The original 1D/2D decomposition, AXI/TCDM backends, collective payload format,
response reordering, grant path, and transfer-time accounting are retained.
Transfer allocation still starts at 0; status 0 is a completion count, status 1
is the next allocation ID plus 1, and status 2 is busy. With gather disabled,
DMIDX has no effect and config bits retain their original interpretation.

This is a frontend/middle-end extension, not a replacement of the private
backend with the generic calibrated backend. Existing backend constraints still
apply. Baseline experiments exposed stalls with exhausted burst queues, a
delayed-TCDM write overlap issue, and collective metadata changes when switching
descriptors before earlier writes finish. The compatibility tests use immediate
TCDM responses, the existing architecture's 256-entry burst capacity, and idle
barriers around legacy copies. These pre-existing behaviors were not changed.
The earlier generic-model RTL calibration does not establish a cycle-error bound
for this private backend.

## Regression

From the top-level `gvsoc` repository, after its documented environment setup:

```bash
bash scripts/sparse_dma/step.sh soft-hier-build
bash scripts/sparse_dma/step.sh soft-hier-test legacy
bash scripts/sparse_dma/step.sh soft-hier-test legacy-enabled
bash scripts/sparse_dma/step.sh soft-hier-test gather-sync
bash scripts/sparse_dma/step.sh soft-hier-test gather-async
bash scripts/sparse_dma/step.sh soft-hier-test isa
bash scripts/sparse_dma/step.sh test
bash scripts/sparse_dma/step.sh soft-hier-report
```

- Nine legacy cases cover all AXI/TCDM copy directions, 1D page splitting, 2D
  destination padding, and collective types 1, 4, 7, and 31. Runs with gather
  disabled and enabled are compared against the unmodified model's recorded
  cycles, transaction hash, and accounting messages.
- Twelve cases per gather mode cover all index widths, unsigned high indices,
  64-bit generated addresses, partial index words, destination padding, empty
  transfers, descriptor snapshots, ordered completion, and offload stalls/grants.
  Asynchronous mode includes out-of-order AXI reads and denied index requests.
- The ISA test runs real instructions on the private Snitch core: a gather,
  followed by collective type 4 using the mask programmed before the gather.
  It checks copied bytes and instruction return values. It uses `rv32imafd`
  because the existing RedMule encodings overlap compressed instructions.

Logs and the generated report are in `build/sparse_dma/soft_hier_old/`.
