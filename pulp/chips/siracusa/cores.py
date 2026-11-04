# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Siracusa cores: iss_v2 Ri5ky with the Xf8 and Xfaux smallfloat extensions
on top of the Ri5ky ones (PulpV2, Xf16, Xf16alt, Xfvec)."""

from cpu.iss.isa_gen.isa_smallfloats import Xf8, Xfaux
from pulp.cpu.iss.ri5ky import Ri5ky, Ri5kyConfig


class SiracusaRi5ky(Ri5ky):

    # Own ISA instance, with the extra extensions
    isa_name = 'ri5ky_siracusa'

    def __init__(self, parent, name: str, config: Ri5kyConfig):
        super().__init__(parent, name, config=config, extra_extensions=[Xf8(), Xfaux()])

        # RI5CY does not gate the FPU on mstatus.FS, and the SDK cluster boot
        # code writes mstatus with FS off before running float code
        self.add_c_flags(['-DCONFIG_GVSOC_ISS_NO_MSTATUS_FS=1'])
