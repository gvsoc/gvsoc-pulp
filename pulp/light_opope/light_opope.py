#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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

import gvsoc.systree

class LightOpope(gvsoc.systree.Component):
    """Outer-product engine: p x p fp16 MACs, 2p x 16-bit memory port.

    Drop-in replacement of LightRedmule (same ports and software interface),
    except that X must be stored transposed (N x M). See light_opope.cpp.
    """

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                tcdm_bank_width: int,
                tcdm_bank_number: int,
                p: int,
                elem_size: int=2,
                mac_latency: int=0,   # pipeline drain charged after the last step of a tile
                input_slots: int=2,   # x/w input register slots (2 = double buffering)
                stream_loads: bool=False, # prefetch x/w beats ahead of the input slots (as LightRedmule)
                prefetch_depth: int=128,  # prefetch FIFO size in beats (2 beats per step)
                queue_depth: int=128, # max outstanding memory requests
                loc_base=0):

        super().__init__(parent, name)

        self.add_sources(['pulp/light_opope/light_opope.cpp', 'cpu/iss/flexfloat/flexfloat.c'])

        self.add_properties({
            'tcdm_bank_width'   : tcdm_bank_width,
            'tcdm_bank_number'  : tcdm_bank_number,
            'elem_size'         : elem_size,
            'p'                 : p,
            'mac_latency'       : mac_latency,
            'input_slots'       : input_slots,
            'stream_loads'      : int(stream_loads),
            'prefetch_depth'    : prefetch_depth,
            'queue_depth'       : queue_depth,
            'loc_base'          : loc_base,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm', itf, signature='io')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('done_irq', itf, signature='wire<bool>')
