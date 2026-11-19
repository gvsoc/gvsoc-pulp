#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

# Author: Chi Zhang <chizhang@iis.ee.ethz.ch>

import gvsoc.systree
from typing import ClassVar
from config_tree import Config, cfg_field
from vp.power_config import PowerSourceConfig
from pulp.chips.soft_hier_old.power_models import (
    light_redmule_power_source,
    power_source_config,
    validate_power_profile,
)


class LightRedmulePowerConfig(Config):
    """Power tables of the LightRedMulE, computed from the selected power profile."""

    _defer_parent_init: ClassVar[bool] = True

    gemm_tile_energy: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Energy of one GEMM tile and leakage of the engine"))


class LightRedmuleConfig(Config):
    """Configuration of the SoftHier (old) LightRedMulE."""

    tcdm_bank_width: int = cfg_field(default=0, dump=True, desc="Width of a TCDM bank, in bytes")
    tcdm_bank_number: int = cfg_field(default=0, dump=True, desc="Number of TCDM banks")
    elem_size: int = cfg_field(default=0, dump=True, desc="Size of a matrix element, in bytes")
    ce_height: int = cfg_field(default=0, dump=True, desc="Height of the compute-element array")
    ce_width: int = cfg_field(default=0, dump=True, desc="Width of the compute-element array")
    ce_pipe: int = cfg_field(default=0, dump=True, desc="Number of pipeline stages of a compute element")
    queue_depth: int = cfg_field(default=128, dump=True, desc="Depth of the job queue")
    fold_tiles_mapping: int = cfg_field(default=0, dump=True, desc="1 to fold the tiles mapping")
    power: LightRedmulePowerConfig = cfg_field(default_factory=LightRedmulePowerConfig, desc=(
        "Power tables"))


class LightRedmule(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                tcdm_bank_width: int,
                tcdm_bank_number: int,
                elem_size: int,
                ce_height: int,
                ce_width: int,
                ce_pipe: int,
                queue_depth: int=128,
                fold_tiles_mapping: int=0,
                tech_node: str="5nm",
                power_profile: str="constant"):

        power_profile = validate_power_profile(power_profile)

        config = LightRedmuleConfig(
            tcdm_bank_width=int(tcdm_bank_width), tcdm_bank_number=int(tcdm_bank_number),
            elem_size=int(elem_size), ce_height=int(ce_height), ce_width=int(ce_width),
            ce_pipe=int(ce_pipe), queue_depth=int(queue_depth),
            fold_tiles_mapping=int(fold_tiles_mapping))

        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/chips/soft_hier_old/light_redmule.cpp'])

        self.LOCAL_BUFFER_H    = ce_height;
        self.LOCAL_BUFFER_N    = tcdm_bank_width * tcdm_bank_number // elem_size;
        self.LOCAL_BUFFER_W    = ce_width * (ce_pipe + 1);
        self.num_mac_per_tile  = self.LOCAL_BUFFER_H * self.LOCAL_BUFFER_W * self.LOCAL_BUFFER_N;
        self.redmule_kge       = 100 + ce_height * ce_width * 8.59

        config.power.gemm_tile_energy = power_source_config('gemm_tile_energy',
            light_redmule_power_source(
                num_tile_mac=self.num_mac_per_tile,
                redmule_kge=self.redmule_kge,
                tech_node=tech_node,
                profile=power_profile,
            ))

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_CORE_ACC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'core_acc', signature='io')

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm', itf, signature='io')
