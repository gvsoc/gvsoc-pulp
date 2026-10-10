#!/usr/bin/env python3
# Copyright (C) 2026 ETH Zurich and University of Bologna
# SPDX-License-Identifier: Apache-2.0
"""Elaborate real SoftHier generators and check the stacked-memory contract.

Run with the built simulator's Python environment and PYTHONPATH.
"""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / path) for path in
               ("pulp", "engine/python", "core/models", "gvrun/python", "config_tree", "gapy/bin")]

from pulp.chips.soft_hier_old.flex_cluster import FlexClusterSystem
from pulp.chips.soft_hier_old.flex_cluster_arch import FlexClusterArch


class Dram3dConfiguration(unittest.TestCase):
    def system(self, **overrides):
        arch = FlexClusterArch()
        # Deliberately incompatible *unused* HBM controller grouping: a
        # stacked-only mesh must not inherit edge-HBM divisibility restrictions.
        arch.num_cluster_x, arch.num_cluster_y = 2, 3
        arch.num_node_per_ctrl = 4
        arch.hbm_chan_placement = [0, 0, 0, 0]
        arch.hbm_node_aliase = 4
        arch.num_core_per_cluster = 1
        arch.dram3d_enable = 1
        for key, value in overrides.items():
            setattr(arch, key, value)
        with patch.dict(os.environ, {"SOFTHIER_ARCH_FILE": ""}), patch(
                "pulp.chips.soft_hier_old.flex_cluster.FlexClusterArch", return_value=arch):
            return FlexClusterSystem(None, "chip", None).get_json_config()

    def test_stacked_only_and_shared_destination(self):
        cfg = self.system()
        channels = [key for key in cfg if key.startswith("dram3d_chan_")]
        self.assertEqual(len(channels), 6)
        self.assertFalse(any("hbm_ctrl" in key or "hbm_chan" in key for key in cfg))
        maps = cfg["data_noc"]["mappings"]
        self.assertEqual(len(maps), 12)
        for cid in range(6):
            dram = maps[f"dram3d_{cid}"]
            tcdm = maps[f"cluster_{cid}"]
            self.assertEqual(dram["base"], 0x10000000000 + cid * 0xc0000000)
            self.assertEqual((dram["x"], dram["y"]), (cid % 2 + 1, cid // 2 + 1))
            self.assertEqual(dram["target"], tcdm["target"])
            self.assertFalse(dram["rm_base"])
            self.assertTrue(tcdm["rm_base"])

    def test_disabled(self):
        cfg = self.system(dram3d_enable=0)
        self.assertFalse(any(key.startswith("dram3d_chan_") for key in cfg))
        self.assertEqual(len(cfg["data_noc"]["mappings"]), 6)
        self.assertEqual(cfg["cluster_0"]["wide_axi_goto_tcdm"]["bandwidth"], 0)

    def test_invalid_apertures(self):
        for overrides in ({"dram3d_node_space": 0}, {"dram3d_node_space": -1},
                          {"dram3d_addr_base": 1 << 64}, {"dram3d_addr_base": 0},
                          {"dram3d_addr_base": 0x30000000}, {"dram3d_type": ""}):
            with self.subTest(overrides=overrides), self.assertRaises(ValueError):
                self.system(**overrides)


if __name__ == "__main__":
    unittest.main()
