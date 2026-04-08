# Copyright (C) 2026 OpenHW Group
# SPDX-License-Identifier: Apache-2.0
#
# Python wrapper for the CV32E40P virtual exit device.
# Terminates GVSOC simulation when the test program writes to 0x2000_0004.

import gvsoc.systree as st


class Cv32e40pExitDevice(st.Component):
    """
    CV32E40P virtual peripheral status flags device.

    Memory map (base-relative):
      +0x00  VP status flags   (test_passed / test_failed — sink)
      +0x04  exit_valid write  → quits GVSOC simulation with exit_value = wdata
      +0x08  sig_start_addr    (sink)
      +0x0C  sig_end_addr      (sink)
      +0x10  sig_write trigger → quits GVSOC simulation with exit_value = 0
    """

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.set_component('devices.cv32e40p_exit.cv32e40p_exit_device')

    def i_INPUT(self) -> st.SlaveItf:
        return st.SlaveItf(self, 'input', signature='io')
