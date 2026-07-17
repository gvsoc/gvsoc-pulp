# Overview

The CI test executes `fcm.py`. This Python script uses `ficlib` to inject bitflips in 3 runs. It is designed to result in 1 MASKED, 1 SDC, and 1 DUE outcome.

The campaign runs the `pulp-open:chip/soc/fic=true` target, the variant that instantiates the FIC. The FIC is added at generation time, so this is a distinct build artifact rather than a runtime option: the target is listed in the root Makefile's `TARGETS` and is built and installed by the normal SDK build.

Run it directly with:

```
python3 fcm.py
```

`gvsoc` is taken from `PATH` (`sourceme.sh` points it at the install) and the campaign writes its fault files and per-thread work dirs under `build/`. Override with `--gvsoc`, `--work-dir`, `--target`, or `--binary`.
