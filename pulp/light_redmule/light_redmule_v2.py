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
from gvsoc.signature import IoV2SingleReq


class LightRedmule(gvsoc.systree.Component):
    """io_v2 sibling of :class:`pulp.light_redmule.light_redmule.LightRedmule`.

    Same engine (tiling, buffers, matmul, HWPE register protocol, slot pool and
    timing model); only the IO ports move to the v2 protocol
    (``light_redmule_v2.cpp``).

    All three memory-mapped ports are IoV2SingleReq:

    - the two register interfaces (``input``, the plain one, and ``input_v2``,
      the HWPE ACQUIRE / COMMIT one magia uses) answer inline today but the
      model keeps a parked-query path (the ACKNOWLEDGE state), so the stricter
      IoV2Sync contract would be wrong;
    - the TCDM master issues one access per bandwidth-sized block and correlates
      the reply by request identity (the slot pool), which is exactly the
      single-req contract; it tolerates inline, async and denied answers.
    """

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
                loc_base=0): #here we might add also local size to check that tcdm requests do not overflow...

        super().__init__(parent, name)

        self.add_sources(['pulp/light_redmule/light_redmule_v2.cpp', 'cpu/iss/flexfloat/flexfloat.c'])

        self.add_properties({
            'tcdm_bank_width'   : tcdm_bank_width,
            'tcdm_bank_number'  : tcdm_bank_number,
            'elem_size'         : elem_size,
            # 'ce_height'         : ce_height,
            # 'ce_width'          : ce_width,
            # WARNING!!! Perform a height width inversion as per Chi's suggestion. Orignal Light_RedMulE model was based on a temp RedMulE architecture.
            'ce_height'         : ce_width,
            'ce_width'          : ce_height,
            'ce_pipe'           : ce_pipe,
            'queue_depth'       : queue_depth,
            'fold_tiles_mapping': fold_tiles_mapping,
            'loc_base'          : loc_base,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2SingleReq())

    def i_INPUT_V2(self) -> gvsoc.systree.SlaveItf:
        """HWPE-style register interface (ACQUIRE / COMMIT_TRIGGER / SOFT_CLEAR).

        The ``_V2`` in the name is the *register protocol* generation, not the IO
        protocol — it predates io_v2 and has nothing to do with it.
        """
        return gvsoc.systree.SlaveItf(self, 'input_v2', signature=IoV2SingleReq())

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
        self.itf_bind('tcdm', itf, signature=IoV2SingleReq())

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('done_irq', itf, signature='wire<bool>')
