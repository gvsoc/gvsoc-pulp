#!/usr/bin/env python3

import argparse
import os
import sys

import ficlib.poi_helpers as poi_helpers
from ficlib.fault_helpers import mem_bitflip_req
from ficlib.campaign_manager import (
    CampaignManager,
    GVSOC_RUN_SUCCESS,
    GVSOC_RUN_ERROR,
    GVSOC_RUN_TIMEOUT,
)

FIC = 'chip/soc/fic'

TOTAL_RUNS = 3

EXPECTED_CLASSIFICATION = {
    "0-0": "MASKED",
    "0-1": "SDC",
    "0-2": "DUE",
}

NB_PE                   = 8
NB_CLUSTER              = 4

PRINT_INJECTIONS        = True
PRINT_DETAILS           = True

def pick_device(campaign, regex, name):
    devices = campaign.get_matching_devices(campaign.all_mems, regex)
    if not devices:
        raise RuntimeError(f"Could not find memory device for {name}, regex={regex}")

    dev = devices[0]
    return {
        "path": dev,
        "fic": campaign.mem_to_fic[dev],
        "target": campaign.mem_to_target[dev],
        "size": campaign.mem_to_size[dev],
    }

def build_fixed_faults(campaign):
    cycles = campaign.golden_cycles[FIC]

    l2_priv0 = pick_device(campaign, r".*priv0.*", "L2 private 0")

    return [
        [
            # MASKED
            mem_bitflip_req(
                target=l2_priv0["target"],
                addr=0x10,
                bit=0,
                delay=10000,
                fic=l2_priv0["fic"],
                path=l2_priv0["path"],
            )
        ],
        [
            # SDC
            mem_bitflip_req(
                target=l2_priv0["target"],
                addr=0x868,
                bit=0,
                delay=1060,
                fic=l2_priv0["fic"],
                path=l2_priv0["path"],
            )
        ],
        [
            # DUE
            mem_bitflip_req(
                target=-1,
                addr=0x1c0080b6,
                bit=0,
                delay=33590,
                fic=FIC,
            )
        ],
    ]

def get_observed_classification(campaign):
    observed = {
        f"0-{run_id}": "MASKED"
        for run_id in range(campaign.total_runs)
    }

    all_faulty_runs = [
        run
        for thread_runs in campaign.faulty_runs
        if thread_runs
        for run in thread_runs
    ]

    for run_id, exit_code, faults, corrupted_pois in all_faulty_runs:
        if exit_code == GVSOC_RUN_SUCCESS and corrupted_pois:
            observed[run_id] = "SDC"
        elif exit_code == GVSOC_RUN_ERROR:
            observed[run_id] = "DUE"
        elif exit_code == GVSOC_RUN_TIMEOUT:
            observed[run_id] = "TIMEOUT"
        elif exit_code == GVSOC_RUN_SUCCESS:
            observed[run_id] = "MASKED"
        else:
            observed[run_id] = f"UNKNOWN(exit_code={exit_code})"

    return observed


def check_expected_classification(campaign):
    observed = get_observed_classification(campaign)

    print("\nExpected FI classification:")
    for run_id, classification in EXPECTED_CLASSIFICATION.items():
        print(f"  {run_id}: {classification}")

    print("\nObserved FI classification:")
    for run_id, classification in observed.items():
        print(f"  {run_id}: {classification}")

    if observed != EXPECTED_CLASSIFICATION:
        raise RuntimeError(
            "Fault injection classification mismatch: "
            f"expected {EXPECTED_CLASSIFICATION}, got {observed}"
        )

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--builddir", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--target", required=True)
    args = parser.parse_args()

    builddir = os.path.abspath(args.builddir)
    binary = os.path.abspath(args.binary)

    pois = poi_helpers.find_pois(
        binary,
        select=poi_helpers.ALL_SYMBOLS,
        ignore=[],
    )

    for poi in pois:
        poi.checker_path = FIC
        poi.target = -1

    campaign = CampaignManager(
        pois=pois,
        fics=[FIC],
        target=args.target,
        binary=binary,
        builddir=builddir,
        config_opts=(
            f"--config-opt=cluster/nb_pe={NB_PE} "
            f"--config-opt=**/nb_cluster={NB_CLUSTER}"
        ),
        print_injections=PRINT_INJECTIONS,
        print_details=PRINT_DETAILS,
        threads=1,
        total_runs=TOTAL_RUNS,
    )

    campaign.do_golden_run()

    fixed_faults = build_fixed_faults(campaign)

    if len(fixed_faults) != TOTAL_RUNS:
        raise RuntimeError(
            f"Expected {TOTAL_RUNS} hardcoded fault runs, got {len(fixed_faults)}"
        )

    def fault_generator(tid, run_id):
        return fixed_faults[run_id]

    campaign.fault_generator = fault_generator
    campaign.start_workers()

    check_expected_classification(campaign)

    return 0

if __name__ == "__main__":
    sys.exit(main())
