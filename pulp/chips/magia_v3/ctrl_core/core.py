# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
from cpu.iss.isa_gen.isa_smallfloats import Xf16, Xf16alt
from pulp.cv32e40p.cv32e40p import Cv32e40p
from pulp.cv32e40p.cv32e40p_config import Cv32e40pConfig


class CV32CtrlCore(Cv32e40p):
    """Control core of the tile.

    The core is the CV32E40P of the tile RTL (magia_tile.sv): COREV_PULP and
    COREV_CLUSTER set (so cv.elw sleeps until the event unit answers), FPU with
    ZFINX and 29 HPM counters. The SDK builds for zhinxmin on top of it, whose
    half-precision conversions gvsoc implements in the Xf16 subsets.

    It boots from the address it receives on i_ENTRY once i_FETCHEN is raised,
    so it starts with fetch disabled. Its mtvec starts at ``mtvec_addr``: the
    tile RTL ties mtvec_addr_i to boot_addr_i, and the SDK crt0 relies on it
    (the vector table is at the start of the binary, the reset entry at +0x80).
    """
    def __init__(self, parent: gvsoc.systree.Component, name: str, core_id: int=0,
            mtvec_addr: int=0):

        config = Cv32e40pConfig(isa='rv32imfc', zfinx=True, corev_pulp=True,
            corev_cluster=True, num_mhpmcounters=29, hart_id=core_id,
            fetch_enable=False, htif=False, mtvec_addr=mtvec_addr)

        # Xf16 has to come before Xf16alt, whose handlers use the float
        # helpers rvXf16.hpp includes
        super().__init__(parent, name, config=config,
            extra_extensions=[Xf16(), Xf16alt()])
