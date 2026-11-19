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
# Description: GVSoC support for MemPool/TeraNoC DPI-style result checks.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#

from typing import Annotated
import gvsoc.systree
from config_tree import Config, cfg_field
from gvrun.runtime import Runtime


class MempoolDpiCheckerConfig(Config):
    nb_banks: int = cfg_field(default=0, desc="Number of L2 banks")
    bank_width: int = cfg_field(default=0, desc="Width of an L2 bank, in bytes")
    interleave: int = cfg_field(default=0, desc="L2 interleaving factor")
    l2_base: int = cfg_field(default=0, fmt="hex", desc="Base address of the L2")
    l2_size: int = cfg_field(default=0, fmt="hex", desc="Size of the L2")
    check_count_addr: Annotated[int, Runtime] = cfg_field(default=0, fmt="hex", desc=(
        "Address of the check count symbol of the binary"))
    check_table_addr: Annotated[int, Runtime] = cfg_field(default=0, fmt="hex", desc=(
        "Address of the check table symbol of the binary"))


class MempoolDpiChecker(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, *,
                 nb_banks: int, bank_width: int, interleave: int,
                 l2_base: int, l2_size: int,
                 check_count_addr: int=0, check_table_addr: int=0):

        super().__init__(parent, name, config=MempoolDpiCheckerConfig(nb_banks=nb_banks,
            bank_width=bank_width, interleave=interleave, l2_base=l2_base, l2_size=l2_size,
            check_count_addr=check_count_addr, check_table_addr=check_table_addr))

        self.add_sources(['pulp/teranoc/mempool_dpi_checker.cpp'])

    def set_symbols(self, check_count_addr: int, check_table_addr: int):
        self.get_config().check_count_addr = check_count_addr
        self.get_config().check_table_addr = check_table_addr

