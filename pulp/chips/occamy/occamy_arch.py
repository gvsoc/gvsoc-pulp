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

from pulp.snitch.snitch_cluster.snitch_cluster import ClusterArch, Area


class OccamyArchProperties:

    def __init__(self):
        self.nb_quadrant             = 6
        self.nb_cluster_per_quadrant = 4
        self.nb_core_per_cluster     = 9
        self.hbm_size                = 0x80000000
        self.hbm_type                = 'simple'


    def declare_target_properties(self, target):

        self.hbm_size = target.declare_user_property(
            name='hbm_size', value=self.hbm_size, cast=int, description='Size of the HBM external memory'
        )

        self.hbm_type = target.declare_user_property(
            name='hbm_type', value=self.hbm_type, allowed_values=['simple', 'dramsys'],
            description='Type of the HBM external memory'
        )

        self.nb_quadrant = target.declare_user_property(
            name='soc/nb_quadrant', value=self.nb_quadrant, cast=int, description='Number of quadrants'
        )

        self.nb_cluster_per_quadrant = target.declare_user_property(
            name='soc/quadrant/nb_cluster', value=self.nb_cluster_per_quadrant, cast=int, description='Number of clusters per quadrant'
        )

        self.nb_core_per_cluster = target.declare_user_property(
            name='soc/quadrant/cluster/nb_core', value=self.nb_core_per_cluster, cast=int, description='Number of cores per cluster'
        )




class OccamyArch:

    def __init__(self, target, properties=None):

        if properties is None:
            properties = OccamyArchProperties()

        properties.declare_target_properties(target)

        self.chip = OccamyArch.Chip(properties)
        self.hbm = OccamyArch.Hbm(properties)

    class Hbm:

        def __init__(self, properties):
            self.size = properties.hbm_size
            self.type = properties.hbm_type

    class Chip:

        def __init__(self, properties):

            self.soc = OccamyArch.Chip.Soc(properties)

        class Soc:

            def __init__(self, properties):
                self.nb_quadrant = properties.nb_quadrant
                current_hartid = 0

                self.debug          = Area(    0x0000_0000,     0x0000_0fff)
                self.bootrom        = Area(    0x0100_0000,     0x0002_0000)
                self.soc_ctrl       = Area(    0x0200_0000,     0x0000_1000)
                self.fll_system     = Area(    0x0200_1000,     0x0000_0400)
                self.fll_periph     = Area(    0x0200_1400,     0x0000_0400)
                self.fll_hbm2d      = Area(    0x0200_1800,     0x0000_0400)
                self.uart           = Area(    0x0200_2000,     0x0000_1000)
                self.gpio           = Area(    0x0200_3000,     0x0000_1000)
                self.i2c            = Area(    0x0200_4000,     0x0000_1000)
                self.chip_ctrl      = Area(    0x0200_5000,     0x0000_1000)
                self.timer          = Area(    0x0200_6000,     0x0000_1000)
                self.hbm_xbar_cfg   = Area(    0x0300_0000,     0x0002_0000)
                self.clint          = Area(    0x0400_0000,     0x0010_0000)
                self.pcie_cfg       = Area(    0x0500_0000,     0x0002_0000)
                self.hbi_wide_cfg   = Area(    0x0600_0000,     0x0001_0000)
                self.hbi_narrow_cfg = Area(    0x0700_0000,     0x0001_0000)
                self.hbm_cfg_top    = Area(    0x0800_0000,     0x0040_0000)
                self.hbm_cfg_phy    = Area(    0x0900_0000,     0x0010_0000)
                self.hbm_cfg_seq    = Area(    0x0a00_0000,     0x0001_0000)
                self.hbm_cfg_ctrl   = Area(    0x0a80_0000,     0x0001_0000)
                self.quad_cfg       = Area(    0x0b00_0000,     0x0001_0000)
                self.plic           = Area(    0x0c00_0000,     0x0400_0000)
                self.quadrant       = Area(    0x1000_0000,     0x0010_0000)
                self.sys_idma_cfg   = Area(    0x1100_0000,     0x0001_0000)
                self.pcie_0         = Area(    0x2000_0000,     0x2800_0000)
                self.pcie_1         = Area(    0x4800_0000,     0x2800_0000)
                self.spm_narrow     = Area(    0x7000_0000,     0x0008_0000)
                self.spm_wide       = Area(    0x7100_0000,     0x0010_0000)
                self.hbm_0_alias    = Area(    0x8000_0000,     0x4000_0000)
                self.hbm_1_alias    = Area(    0xc000_0000,     0x4000_0000)
                self.wide_zero_mem  = Area(  0x1_000_0000,    0x2_0000_0000)
                self.hbm_0          = Area( 0x10_0000_0000,     0x4000_0000)
                self.hbm_1          = Area( 0x10_4000_0000,     0x4000_0000)
                self.hbm_2          = Area( 0x10_8000_0000,     0x4000_0000)
                self.hbm_3          = Area( 0x10_c000_0000,     0x4000_0000)
                self.hbm_4          = Area( 0x11_0000_0000,     0x4000_0000)
                self.hbm_5          = Area( 0x11_4000_0000,     0x4000_0000)
                self.hbm_6          = Area( 0x11_8000_0000,     0x4000_0000)
                self.hbm_7          = Area( 0x11_c000_0000,     0x4000_0000)
                self.hbi            = Area(0x100_0000_0000, 0x100_0000_0000)

                # Account one hartid for cva6
                current_hartid += 1

                self.quadrants = []
                for id in range(0, self.nb_quadrant):
                    self.quadrants.append(
                        OccamyArch.Chip.Quadrant(properties, self.quadrant_base(id), current_hartid)
                    )
                    current_hartid += self.quadrants[id].nb_core

            def quad_cfg_base(self, id:int):
                return self.quad_cfg.base + id * self.quad_cfg.size

            def quadrant_base(self, id:int):
                return self.quadrant.base + id * self.quadrant.size

            def get_quadrant(self, id: int):
                return self.quadrants[id]



        class Quadrant:
            def __init__(self, properties, base, first_hartid):
                self.nb_cluster = properties.nb_cluster_per_quadrant
                self.base = base

                self.cluster  = Area(base, 0x0004_0000)

                self.clusters = []
                current_hardid = first_hartid
                for id in range(0, self.nb_cluster):
                    self.clusters.append(
                        ClusterArch(properties, self.get_cluster_base(id), current_hardid)
                    )
                    current_hardid += self.clusters[id].nb_core

                self.nb_core = current_hardid - first_hartid

            def get_cluster_base(self, id:int):
                return self.cluster.base + id * self.cluster.size

            def get_cluster(self, id: int):
                return self.clusters[id]
