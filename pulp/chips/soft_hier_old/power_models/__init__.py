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
