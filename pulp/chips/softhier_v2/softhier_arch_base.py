#
# Copyright (C) 2020 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Authors: Chi Zhang <chizhang@ethz.ch>, Siim Rausi <srausi@student.ethz.ch>

from gvrun.attribute import Tree


def get_arch_overrides(component, arch_cls):
    """
    Read the overrides of the arch attributes, which are all gvrun target
    parameters of component.

    Usage (from a topology's SoftHierSystem.__init__, which receives the
    systree Component as `self`):
        arch = SoftHierArch(**get_arch_overrides(self, SoftHierArch))
    Overridden either on the target name, which also applies to the build:
        gvrun --target pulp.chips.softhier_v2.topologies.softhier_2d_mesh_target:system/num_core_per_cluster=2 ...
    or at run time for attributes which do not change the platform shape:
        gvrun ... --parameter system/noc_outstanding=32 run
    """
    from gvrun.parameter import TargetParameter

    overrides = {}
    for field_name, default_value in vars(arch_cls()).items():
        if default_value is None:
            continue
        value = TargetParameter(component, name=field_name, value=default_value,
            description=f'SoftHier arch attribute {field_name}').get_value()
        if value != default_value:
            overrides[field_name] = value
    return overrides


class SoftHierAttributes(Tree):
    """
    Architecture of a SoftHier system as board attributes, from which tools
    like PulpOS read the memory map and the cluster characteristics.
    """

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)
        for field_name, value in vars(arch).items():
            setattr(self, field_name, value)


class SoftHierArchBase:
    """
    Attributes shared identically by every SoftHier topology, plus None
    defaults for the handful of fields only some topology families use
    (num_cluster_x/y/z for mesh/torus families, num_global/local_clusters
    for hierarchical_ring, num_rings/is_torus for the hex families).
    """

    def __init__(self):

        self.num_core_per_cluster    = 1

        # Family-specific fields, not appearing here, can be read by using
        # getattr(arch, 'field_name', None)

        self.cluster_tcdm_bank_width = 4
        self.cluster_tcdm_bank_nb    = 128

        self.cluster_tcdm_base       = 0x00000000
        self.cluster_tcdm_size       = 0x00100000
        self.cluster_tcdm_remote     = 0x30000000

        self.cluster_stack_base      = 0x10000000
        self.cluster_stack_size      = 0x00020000

        self.cluster_zomem_base      = 0x18000000
        self.cluster_zomem_size      = 0x00020000

        self.cluster_reg_base        = 0x20000000
        self.cluster_reg_size        = 0x00000200

        self.instruction_mem_base    = 0x80000000
        self.instruction_mem_size    = 0x00010000

        #Spatz Vector Unit
        self.spatz_num_lane          = 32
        self.spatz_lane_width        = 4

        #System
        self.soc_register_base       = 0x70000000
        self.soc_register_size       = 0x00010000
        self.soc_register_eoc        = 0x70000000

        #NoC
        self.noc_link_width          = 512
        # The NoC routers use wormhole arbitration with a single virtual
        # channel, like the FlooNoC RTL: routing tables whose channel
        # dependency graph has a cycle can deadlock under load. The NoC
        # refuses them unless this is set, and then only warns.
        self.noc_allow_deadlock      = 0


class SoftHierArch2DMesh(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 16
        self.topology                = "2DMesh"
        self.num_cluster_x           = 4
        self.num_cluster_y           = 4
        self.shape_category          = 2
        self.is_torus                = 0
        self.link_latency            = 1
        self.idma_outstand_txn       = 128
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArchTorus(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 64
        self.topology                = "2DTorus"
        self.num_cluster_x           = 8
        self.num_cluster_y           = 8
        self.shape_category          = 2
        self.is_torus                = 1
        self.link_latency            = 1
        self.wraparound_latency      = 5
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArch3D(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 64
        self.topology                = "3DMesh"
        self.num_cluster_x           = 4
        self.num_cluster_y           = 4
        self.num_cluster_z           = 4
        self.shape_category          = 3
        self.is_torus                = 0
        self.link_latency            = 1
        self.idma_outstand_txn       = 256
        self.idma_outstand_burst     = 1024
        self.noc_outstanding         = 256
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArch3DTorus(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 125
        self.topology                = "3DTorus"
        self.num_cluster_x           = 5
        self.num_cluster_y           = 5
        self.num_cluster_z           = 5
        self.shape_category          = 3
        self.is_torus                = 1
        self.link_latency            = 1
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        # Its routing tables have channel dependency cycles
        self.noc_allow_deadlock      = 1
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArchRing(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 32
        self.topology                = "Ring"
        self.shape_category          = 1
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        self.link_latency            = 1
        # Its routing tables have channel dependency cycles
        self.noc_allow_deadlock      = 1
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArchHierRing(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 36
        self.topology                = "HierRing"
        self.num_global_clusters     = 6
        self.num_local_clusters      = 6
        self.shape_category          = 1
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        self.link_latency            = 1
        # Its routing tables have channel dependency cycles
        self.noc_allow_deadlock      = 1
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArchHexaMesh(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 61
        # 1 + 3*num_rings*(num_rings+1)
        self.topology                = "HexaMesh"
        self.num_rings               = 4
        self.shape_category          = 4
        self.is_torus                = 0
        self.link_latency            = 1
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        for k, v in overrides.items():
            setattr(self, k, v)


class SoftHierArchFoldedHexaTorus(SoftHierArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster             = 127
        # 1 + 3*num_rings*(num_rings+1)
        self.topology                = "FoldedHexaTorus"
        self.num_rings               = 6
        self.shape_category          = 4
        self.is_torus                = 1
        self.link_latency            = 1
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256
        self.noc_outstanding         = 64
        # Its routing tables have channel dependency cycles
        self.noc_allow_deadlock      = 1
        for k, v in overrides.items():
            setattr(self, k, v)

TOPOLOGIES = {
    "2d_mesh":           SoftHierArch2DMesh,
    "2d_torus":          SoftHierArchTorus,
    "3d_mesh":           SoftHierArch3D,
    "3d_torus":          SoftHierArch3DTorus,
    "ring":              SoftHierArchRing,
    "hierarchical_ring": SoftHierArchHierRing,
    "hexamesh":          SoftHierArchHexaMesh,
    "folded_hexatorus":  SoftHierArchFoldedHexaTorus,
}


# Backward-compatible alias: existing code that does
# `from pulp.chips.softhier_v2.<topo>.softhier_arch import SoftHierArch`
# and calls `SoftHierArch()` with no topology context still needs a
# concrete class name during the transition; new call sites should use
# TOPOLOGIES[name]() instead.
SoftHierArch = SoftHierArch2DMesh
