# SoftHier component power models

This directory separates characterization assumptions from the LightRedMulE
and memory design models. The numbers are engineering estimates for
comparative thermal studies; they are not silicon-calibrated sign-off data.

The selectable profiles are:

- `constant`: both components use the new 25 °C reference leakage, held fixed.
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
