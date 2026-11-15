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
#         Lorenzo Zuolo, Chips-IT <lorenzo.zuolo@chips.it>
#         Yinrong Li <yinrli@student.ethz.ch>

import gvsoc.systree
from config_tree import Config, cfg_field


class LightRedmuleConfig(Config):
    """Configuration of the light RedMulE matrix engine."""

    tcdm_bank_width: int = cfg_field(default=0, desc=(
        "Width in bytes of a TCDM bank"
    ))
    tcdm_bank_number: int = cfg_field(default=0, desc=(
        "Number of TCDM banks the engine accesses in parallel"
    ))
    elem_size: int = cfg_field(default=0, desc=(
        "Size in bytes of a matrix element"
    ))
    ce_height: int = cfg_field(default=0, desc=(
        "Height of the array of computing elements"
    ))
    ce_width: int = cfg_field(default=0, desc=(
        "Width of the array of computing elements"
    ))
    ce_pipe: int = cfg_field(default=0, desc=(
        "Number of pipeline stages of a computing element"
    ))
    queue_depth: int = cfg_field(default=128, desc=(
        "Depth of the queue of TCDM requests"
    ))
    fold_tiles_mapping: int = cfg_field(default=0, desc=(
        "Non-zero to fold the tile mapping of the accesses"
    ))
    loc_base: int = cfg_field(default=0, fmt="hex", desc=(
        "Base address of the local TCDM"
    ))
    stream_loads: bool = cfg_field(default=False, desc=(
        "True to send the ROUTINE requests ahead of the array (synchronous TCDM only, v1 model)"
    ))


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
                loc_base=0, #here we might add also local size to check that tcdm requests do not overflow...
                stream_loads: bool=False): # send the ROUTINE requests ahead of the array (synchronous TCDM only)

        # WARNING!!! Perform a height width inversion as per Chi's suggestion. Orignal
        # Light_RedMulE model was based on a temp RedMulE architecture.
        super().__init__(parent, name, config=LightRedmuleConfig(
            tcdm_bank_width=tcdm_bank_width, tcdm_bank_number=tcdm_bank_number,
            elem_size=elem_size, ce_height=ce_width, ce_width=ce_height, ce_pipe=ce_pipe,
            queue_depth=queue_depth, fold_tiles_mapping=fold_tiles_mapping,
            loc_base=loc_base,
            stream_loads=stream_loads))

        self.add_sources(['pulp/light_redmule/light_redmule.cpp', 'cpu/iss/flexfloat/flexfloat.c'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_INPUT_V2(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input_v2', signature='io')

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        """Returns the offload port.

        This is used by the core to offload instructions.\n

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        """Binds the offload grant port.

        This port is used for granting instruction which was previously blocked because
        the queue was full.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm', itf, signature='io')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('done_irq', itf, signature='wire<bool>')
