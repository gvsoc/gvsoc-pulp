"""Selectable component power models for the SoftHier platform.

The JSON files in this directory contain technology-normalized characterization
data.  This module scales those data to the instantiated LightRedMulE gate count
or memory capacity and emits the GVSoC linear-table schema.

Two profiles are intentionally provided:

``constant``
    Give both components a plausible reference leakage at 25 C and keep it
    constant.  This is the control case for temperature-feedback experiments.
``temperature_aware``
    Use the same 25 C reference values as ``constant`` and sample
    ``P(T) = P(25 C) * 2**((T - 25 C) / T_double)`` on a temperature grid.
    GVSoC linearly interpolates between samples and clamps outside the grid.
"""

import json
import math
from functools import lru_cache
from pathlib import Path
from typing import Any, Dict


SUPPORTED_POWER_PROFILES = ("constant", "temperature_aware")
REFERENCE_TEMPERATURE_C = 25.0
TEMPERATURE_GRID_C = (25.0, 40.0, 55.0, 70.0, 85.0, 100.0, 115.0, 125.0)
MIB = 1024 * 1024

_MODEL_DIR = Path(__file__).resolve().parent


def validate_power_profile(profile: str) -> str:
    """Return a normalized profile name or raise a useful configuration error."""

    normalized = str(profile).strip().lower().replace("-", "_")
    if normalized not in SUPPORTED_POWER_PROFILES:
        choices = ", ".join(SUPPORTED_POWER_PROFILES)
        raise ValueError(f"unknown SoftHier power profile {profile!r}; choose one of {choices}")
    return normalized


@lru_cache(maxsize=None)
def _load_spec(component: str) -> Dict[str, Any]:
    path = _MODEL_DIR / f"{component}.json"
    with path.open(encoding="utf-8") as stream:
        spec = json.load(stream)

    if spec.get("schema_version") != 1 or spec.get("component") != component:
        raise ValueError(f"invalid component power-model specification: {path}")
    return spec


def _node_spec(component: str, tech_node: str) -> Dict[str, Any]:
    spec = _load_spec(component)
    nodes = spec.get("technology_nodes", {})
    if tech_node not in nodes:
        choices = ", ".join(sorted(nodes))
        raise ValueError(
            f"unsupported technology node {tech_node!r} for {component}; "
            f"choose one of {choices}"
        )
    return nodes[tech_node]


def _key(value: float) -> str:
    return f"{value:g}"


def _linear_table(unit: str, values: Dict[str, Any]) -> Dict[str, Any]:
    return {"type": "linear", "unit": unit, "values": values}


def _single_temperature_values(
    voltage_values: Dict[str, float], scale: float
) -> Dict[str, Any]:
    return {
        _key(REFERENCE_TEMPERATURE_C): {
            str(voltage): {"any": float(value) * scale}
            for voltage, value in voltage_values.items()
        }
    }


def _profiled_leakage_values(
    voltage_values_at_25c: Dict[str, float],
    scale: float,
    doubling_temperature_c: float,
    profile: str,
) -> Dict[str, Any]:
    if profile == "constant":
        temperatures = (TEMPERATURE_GRID_C[0], TEMPERATURE_GRID_C[-1])
    elif profile == "temperature_aware":
        temperatures = TEMPERATURE_GRID_C
    else:
        raise ValueError(f"profile {profile!r} does not use the profiled leakage model")

    values = {}  # type: Dict[str, Any]
    for temperature_c in temperatures:
        temperature_scale = 1.0
        if profile == "temperature_aware":
            temperature_scale = math.pow(
                2.0,
                (temperature_c - REFERENCE_TEMPERATURE_C)
                / doubling_temperature_c,
            )
        values[_key(temperature_c)] = {
            str(voltage): {
                "any": float(reference) * scale * temperature_scale,
            }
            for voltage, reference in voltage_values_at_25c.items()
        }
    return values


def light_redmule_power_source(
    *,
    num_tile_mac: int,
    redmule_kge: float,
    tech_node: str,
    profile: str,
) -> Dict[str, Any]:
    """Build the LightRedMulE event-energy and leakage source table."""

    profile = validate_power_profile(profile)
    node = _node_spec("light_redmule", tech_node)
    source = {
        "dynamic": _linear_table(
            "pJ",
            _single_temperature_values(
                node["dynamic_pj_per_mac"], float(num_tile_mac)
            ),
        )
    }

    # Characterization is normalized in uW/kGE.  This keeps the table reusable
    # when the compute-engine dimensions change.
    leakage_scale = float(redmule_kge) * 1.0e-6
    source["leakage"] = _linear_table(
        "W",
        _profiled_leakage_values(
            node["leakage_uw_per_kge_at_25c"],
            leakage_scale,
            float(node["leakage_doubling_temperature_c"]),
            profile,
        ),
    )

    return source


def memory_power_sources(
    *, size_bytes: int, tech_node: str, profile: str
) -> Dict[str, Any]:
    """Build SRAM background/leakage and per-access energy source tables."""

    profile = validate_power_profile(profile)
    node = _node_spec("memory", tech_node)
    dynamic_values = _single_temperature_values(
        node["dynamic_pj_per_access_byte"], 1.0
    )

    background = {
        "dynamic": _linear_table(
            "W",
            _single_temperature_values(
                {voltage: 0.0 for voltage in node["dynamic_pj_per_access_byte"]},
                1.0,
            ),
        )
    }

    leakage_values = _profiled_leakage_values(
        node["leakage_w_per_mib_at_25c"],
        float(size_bytes) / MIB,
        float(node["leakage_doubling_temperature_c"]),
        profile,
    )

    background["leakage"] = _linear_table("W", leakage_values)
    return {
        "background": background,
        "access_byte": {"dynamic": _linear_table("pJ", dynamic_values)},
    }


def component_model_metadata(component: str, tech_node: str) -> Dict[str, Any]:
    """Return a copy of one node's normalized data for reports/tests."""

    return json.loads(json.dumps(_node_spec(component, tech_node)))


def model_spec(component: str) -> Dict[str, Any]:
    """Return independent, serializable provenance and reference coefficients."""
    return json.loads(json.dumps(_load_spec(component)))


def technology_spec(tech_node: str) -> Dict[str, Any]:
    return dict(_node_spec("technology", tech_node))


def logic_area_kge(component: str, **parameters: Any) -> float:
    """Single area estimate shared by instantiated sources and the floorplan."""
    fields = {"core": (), "spatz": ("function_units", "vrf_bytes", "vlsu_ports"),
              "idma": ("outstanding", "data_width_bits"), "floonoc": ("data_width_bits",),
              "transpose": ("buffer_bytes",)}
    if component not in fields:
        raise ValueError(f"no logic area model for {component}")
    for name in fields[component]:
        value = float(parameters[name])
        if not math.isfinite(value) or value <= 0 or not value.is_integer():
            raise ValueError(f"{component}.{name} must be a positive integer")
    area = _load_spec(component)["area_kge"]
    if component == "core":
        return area["integer_core"] + area["scalar_fpu"]
    if component == "spatz":
        return (area["controller"] + area["ipu"]
                + area["fpu_per_unit"] * parameters["function_units"]
                + area["vrf_per_2kib"] * parameters["vrf_bytes"] / 2048
                + area["vlsu_per_4ports"] * parameters["vlsu_ports"] / 4)
    if component == "idma":
        return (area["control"] + area["per_outstanding"] * parameters["outstanding"]
                + area["per_32bit_datapath"] * parameters["data_width_bits"] / 32)
    if component == "floonoc":
        part = parameters["part"]
        if part not in ("router", "ni"):
            raise ValueError(f"invalid NoC part {part!r}")
        return area[f"{part}_fixed"] + area[f"{part}_datapath"] * parameters["data_width_bits"] / 512
    if component == "transpose":
        return area["control"] + area["per_buffer_byte"] * parameters["buffer_bytes"]
    raise ValueError(f"no logic area model for {component}")


def _estimated_source(energy_pj: float, tech_node: str) -> Dict[str, Any]:
    tech = technology_spec(tech_node)
    voltages = sorted({0.6, min(0.8, tech["nominal_voltage_v"]), tech["nominal_voltage_v"]})
    values = {_key(v): float(energy_pj) * tech["capacitance_scale"] * (v / 0.8) ** 2
              for v in voltages}
    return {"dynamic": _linear_table("pJ", _single_temperature_values(values, 1.0))}


def logic_power_sources(component: str, *, tech_node: str, profile: str,
                        frequency_hz: float = 1.0e9, estimate_scale: float = 1.0,
                        **parameters: Any) -> Dict[str, Any]:
    """Resolve event energies and residual clock/leakage power for one instance.

    Background power is specified at the configured fixed frequency. Event
    energies must NOT also be multiplied by frequency. The estimate multiplier
    supports controlled sensitivity runs and does not affect area or timing.
    """
    profile = validate_power_profile(profile)
    if not math.isfinite(estimate_scale) or estimate_scale <= 0:
        raise ValueError("estimate_scale must be finite and positive")
    if not math.isfinite(frequency_hz) or frequency_hz <= 0:
        raise ValueError("frequency_hz must be finite and positive")
    spec = _load_spec(component)
    gate_count = logic_area_kge(component, **parameters)
    reference_parameters = dict(spec.get("reference", {}))
    if component == "floonoc":
        part = parameters["part"]
        events = spec[f"{part}_events_pj"]
        clock_mw = spec[f"{part}_background_mw_at_1ghz"]
        reference_parameters["part"] = part
    else:
        events = dict(spec["events_pj"])
        clock_mw = spec["background_mw_at_1ghz"]
    if component == "transpose":
        reference_parameters["buffer_bytes"] = 4096
    reference_area = logic_area_kge(component, **reference_parameters)
    sources = {name: _estimated_source(value * estimate_scale, tech_node)
               for name, value in events.items()}
    if component == "spatz":
        for operation, energy in spec["arithmetic_pj_per_fp64_element"].items():
            for precision, factor in spec["precision_factors"].items():
                sources[f"{operation}_{precision}"] = _estimated_source(
                    energy * factor * estimate_scale, tech_node)
    # Convert pJ/cycle at 1 GHz to W at the fixed architecture frequency.
    background = _estimated_source(clock_mw * gate_count / reference_area * estimate_scale, tech_node)
    background["dynamic"]["unit"] = "W"
    for voltages in background["dynamic"]["values"].values():
        for frequencies in voltages.values():
            frequencies["any"] *= frequency_hz * 1.0e-12
    node = _node_spec("light_redmule", tech_node)
    background["leakage"] = _linear_table("W", _profiled_leakage_values(
        node["leakage_uw_per_kge_at_25c"], gate_count * 1.0e-6 * estimate_scale,
        node["leakage_doubling_temperature_c"], profile))
    sources["background"] = background
    return sources


def core_instruction_group(label: str) -> int:
    """Stable groups shared by the generated decoder and instruction tables."""
    name = label[2:] if label.startswith("c.") else label
    if name.startswith("v"):
        return 6
    if name.startswith(("div", "rem")):
        return 4
    if name.startswith("mul"):
        return 3
    if name in ("flw", "fld", "flh", "flb", "fsw", "fsd", "fsh", "fsb"):
        return 1
    if name.startswith("f") and not name.startswith(("fence", "frep")):
        return 5
    if name.startswith(("lb", "lh", "lw", "ld", "sb", "sh", "sw", "sd", "amo", "lr.", "sc.")):
        return 1
    if name.startswith(("b", "j")):
        return 2
    if name.startswith(("csr", "wfi", "ecall", "ebreak", "dm", "frep")):
        return 7
    return 0


def core_power_sources(**kwargs: Any) -> Dict[str, Any]:
    sources = logic_power_sources("core", **kwargs)
    sources["insn_groups"] = [sources[name] for name in _load_spec("core")["instruction_groups"]]
    sources["separate_scalar_domain"] = True
    return sources
