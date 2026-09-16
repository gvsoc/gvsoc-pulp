# SoftHier component power models

This directory separates characterization assumptions from the component
design models. The numbers are engineering estimates for
comparative thermal studies; they are not silicon-calibrated sign-off data.

The selectable profiles are:

- `constant`: every component uses its 25 °C reference leakage, held fixed.
- `temperature_aware`: the same references follow
  `P_leak(T) = P_leak(25 °C) * 2^((T - 25 °C) / T_double)`.

The exponential law is sampled at 25, 40, 55, 70, 85, 100, 115, and 125 °C.
GVSoC linearly interpolates between samples and clamps outside the characterized
range. Smaller nodes use higher per-gate leakage and shorter doubling
temperatures, while their existing dynamic-energy points fall with scaling:

| Node | Logic doubling | SRAM doubling | Logic reference | SRAM reference |
|---|---:|---:|---:|---:|
| 22 nm | 30 °C | 35 °C | 1.2 µW/kGE @ 1.0 V | 12 mW/MiB @ 1.0 V |
| 12 nm | 27 °C | 32 °C | 1.8 µW/kGE @ 0.9 V | 15 mW/MiB @ 0.9 V |
| 7 nm | 24 °C | 29 °C | 2.6 µW/kGE @ 0.8 V | 17 mW/MiB @ 0.8 V |
| 5 nm | 22 °C | 27 °C | 3.4 µW/kGE @ 0.7 V | 20 mW/MiB @ 0.7 V |

LightRedMulE leakage is scaled by the model's estimated kGE; SRAM leakage is
scaled by instantiated capacity. Dynamic tables retain the original values.

Select a profile through the top-level co-simulation command, for example:

```sh
make co-simulation RUN_NAME=leakage_temp \
  SOFTHIER_POWER_PROFILE=temperature_aware
```

## Expanded logic models (version 2)

`core.json`, `spatz.json`, `idma.json`, `floonoc.json`, and `transpose.json`
contain reference energies, area rules, assumptions, and primary-source links.
`technology.json` defines nominal voltages, capacitance scaling, and placed-cell
area scaling. Area is estimated from gate equivalents, with 66% placement
utilization applied by the floorplan exporter; SRAM uses a separate bit-cell
area estimate. Neither is a placed-and-routed floorplan.

New event energies use `E(node,V) = E(12nm,0.8V) * Cscale(node) * (V/0.8)^2`.
Capacitance scales are 1.6, 1.0, 0.8, and 0.65 for 22, 12, 7, and 5 nm.
These are intentionally coarse technology projections, not a universal process
scaling law. Event energies are **not** multiplied by clock frequency. A small
residual clock budget, specified in mW at 1 GHz, is scaled by area, voltage,
and the configured fixed frequency. Clock gating and DVFS are not modeled.
Logic leakage uses the same per-kGE table and temperature law as RedMulE.

| Model | Activity charged | Exclusions / important assumptions |
|---|---|---|
| Scalar core | Executed instruction class, stall cycles, residual clocks | Separate scalar FPU assumed (167 kGE total); no cache area included |
| Spatz | Active vector elements by operation and precision, VRF bytes, completed VLSU bytes | Scalar core and TCDM excluded; scalar-operand versus vector-operand VRF traffic distinguished |
| iDMA | Accepted 1-D row, legalized burst, data bytes once | Does not include destination/source memory or NoC |
| NoC router | Outbound and return packets on actual routed branches; payload only in its data-carrying direction | Router/adjacent wire energy is lumped per byte per physical hop; no second width multiplier |
| NoC interface | Injection/completion control and payload | Coefficient covers initiator-side interface; destination adapters/local AXI crossbars are not modeled separately |
| Transpose | Started operation and transferred tile bytes | Coarse estimate, not paper-calibrated; host scratch arrays are not treated as RTL buffers |

Spatz FP64 add/multiply/FMA arithmetic references are 12.5/16.2/18.1 pJ per
active element. The VRF reference is 5 pJ for 24 bytes, counted separately.
Lower precision, divide, custom exponential, integer/vector miscellaneous,
and residual-clock coefficients are estimates. The model resolves the actual
configured vector-register capacity, FPU count, and VLSU port count; the
implementation architecture has a **1 KiB VRF per Spatz**, not the paper's
2 KiB reference. [Spatz, Figs. 9–12](https://arxiv.org/pdf/2309.10137v2).

The iDMA paper provides structural scaling guidance, not a transferable
universal energy-per-byte number. The 0.1 pJ/byte coefficient is an estimate,
sanity-checked against a transfer experiment's DMA power budget.
[iDMA, Section IV](https://arxiv.org/html/2305.05240v2).

The router reference is **0.15 pJ/byte/hop**, not pJ/bit. Reference router/NI
areas are 168/28 kGE at 512 data bits. Collective reduction energy is an
additional, uncalibrated arithmetic estimate: requests and responses follow
their actual branch segments, without charging a shared prefix per child.
[FlooNoC, Fig. 9 and Section VI-D](https://arxiv.org/pdf/2409.17606v2).

`power_estimate_scale` in `FlexClusterArch` supports sensitivity checks: it
multiplies **new logic** event, clock, and leakage estimates, leaving timing,
area, and the existing RedMulE/memory coefficients unchanged. The factory
accepts a custom fixed frequency, but the current SoftHier target is 1 GHz.

## Thermal domains and limitations

Scalar sources live under `peN/scalar_power`; vector sources under `peN/ara`.
This allows separate feedback without overlapping scalar/vector subtree sums.
Routers and interfaces retain their real `/chip/data_noc/...` and
`/chip/sync_bus/...` paths, while their floorplan regions sit beside the
corresponding cluster. Each power column maps to exactly one floorplan region.

HBM/PHY, local crossbars, registers, and global clock distribution beyond the
component budgets remain uncharacterized. The `others` region reserves 5% of
support area and defaults to zero power; this is not a whole-chip sign-off
power number. SRAM thermal area uses architectural capacity; existing GVSoC
TCDM power also includes its small padding/synchronization allocations.

The initial 26.85 °C temperature differs from the 25 °C leakage reference.
Consequently, the temperature-aware case starts with higher leakage even
before self-heating. Reports must separate that reference offset from the
additional increase during execution. Short kernels do not establish thermal
steady state, and fixed-frequency power feedback does not model thermal timing
degradation or throttling.
